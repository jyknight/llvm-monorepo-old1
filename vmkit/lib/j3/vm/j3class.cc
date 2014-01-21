#include <stdio.h>
#include <string.h>
#include <vector>

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"

#include "j3/j3.h"
#include "j3/j3class.h"
#include "j3/j3classloader.h"
#include "j3/j3reader.h"
#include "j3/j3constants.h"
#include "j3/j3method.h"
#include "j3/j3mangler.h"
#include "j3/j3object.h"
#include "j3/j3thread.h"
#include "j3/j3field.h"
#include "j3/j3attribute.h"
#include "j3/j3codegen.h"

using namespace j3;

/*  
 *  ------------ J3Type ------------
 */
J3Type::J3Type(J3ClassLoader* loader, const vmkit::Name* name) { 
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&_mutex, &attr);
	_loader = loader; 
	_name = name; 
}

void* J3Type::getSymbolAddress() {
	return this;
}

J3VirtualTable* J3Type::vt() { 
	return _vt; 
}

void J3Type::dump() {
	fprintf(stderr, "Type: %s", name()->cStr());
}

J3ObjectHandle* J3Type::javaClass(bool doPush, J3ObjectHandle* protectionDomain) {
	if(!_javaClass) {
		lock();
		if(!_javaClass) {
			J3* vm = J3Thread::get()->vm();
			J3ObjectHandle* prev = J3Thread::get()->tell();
			_javaClass = loader()->globalReferences()->add(J3ObjectHandle::doNewObject(vm->classClass));
			J3Thread::get()->restore(prev);
			_javaClass->setLong(vm->classClassVMData, (int64_t)(uintptr_t)this);
			vm->classClassInit->invokeSpecial(_javaClass);
		}
		unlock();
	}
	return doPush ? J3Thread::get()->push(_javaClass) : _javaClass;
}

void J3Type::doNativeName() {
	J3::internalError("should not happen");
}

char* J3Type::nativeName() {
	if(!_nativeName)
		doNativeName();
	return _nativeName;
}

uint32_t J3Type::nativeNameLength() {
	if(!_nativeNameLength)
		doNativeName();
	return _nativeNameLength;
}

J3ArrayClass* J3Type::getArray(uint32_t prof, const vmkit::Name* name) {
	if(!_array) {
		lock();
		if(!_array) {
			_array = new(loader()->allocator()) J3ArrayClass(loader(), this, name);
		}
		unlock();
	}

	return prof > 1 ? _array->getArray(prof-1) : _array;
}

uint64_t J3Type::getSizeInBits() {
	return 1 << (logSize()+3);
}

bool J3Type::isAssignableTo(J3Type* parent) {
	resolve();
	parent->resolve();
	return vt()->isAssignableTo(parent->vt());
}

J3Type* J3Type::resolve() {
	if(status < RESOLVED)
		doResolve(0, 0);
	return this;
}

J3Type* J3Type::resolve(J3Field* hiddenFields, uint32_t nbHiddenFields) {
	if(status < RESOLVED)
		doResolve(hiddenFields, nbHiddenFields);
	else
		J3::internalError("trying to resolve class %s with hidden fields while it is already loaded", name()->cStr());
	return this;
}

J3Type* J3Type::initialise() {
	if(status < INITED)
		doInitialise();
	return this;
}

J3Class* J3Type::asClass() {
	if(!isClass())
		J3::internalError("should not happen");
	return (J3Class*)this;
}

J3Layout* J3Type::asLayout() {
	if(!isLayout())
		J3::internalError("should not happen");
	return (J3Layout*)this;
}

J3StaticLayout* J3Type::asStaticLayout() {
	if(!isStaticLayout())
		J3::internalError("should not happen");
	return (J3StaticLayout*)this;
}

J3Primitive* J3Type::asPrimitive() {
	if(!isPrimitive())
		J3::internalError("should not happen");
	return (J3Primitive*)this;
}

J3ArrayClass* J3Type::asArrayClass() {
	if(!isArrayClass())
		J3::internalError("should not happen");
	return (J3ArrayClass*)this;
}

J3ObjectType* J3Type::asObjectType() {
	if(!isObjectType())
		J3::internalError("should not happen");
	return (J3ObjectType*)this;
}

/*  
 *  ------------ J3ObjectType ------------
 */
J3ObjectType::J3ObjectType(J3ClassLoader* loader, const vmkit::Name* name) : J3Type(loader, name) {
}

llvm::Type* J3ObjectType::llvmType() {
	return J3Thread::get()->vm()->typeJ3ObjectPtr;
}

J3Method* J3ObjectType::findMethod(uint32_t access, const vmkit::Name* name, J3Signature* signature, bool error) {
	J3::internalError("should not happen - findMethod: %s::%s\n", J3ObjectType::name()->cStr(), name->cStr());
}

J3ObjectType* J3ObjectType::nativeClass(J3ObjectHandle* handle) {
	return (J3ObjectType*)(uintptr_t)handle->getLong(J3Thread::get()->vm()->classClassVMData);
}

J3ObjectHandle* J3ObjectType::clone(J3ObjectHandle* obj) {
	J3::internalError("should not happen");
}

uint16_t J3ObjectType::access() {
	J3::internalError("should not happen");
}

uint16_t J3ObjectType::modifiers() {
	J3::internalError("should not happen");
}

J3Class* J3ObjectType::super() {
	J3::internalError("should not happen");
}

void J3ObjectType::prepareInterfaceTable() {
	//fprintf(stderr, "prepare interface table of %s\n", name()->cStr());

	uint32_t total = 0;
	J3InterfaceSlotDescriptor* slots = _interfaceSlotDescriptors;

	for(uint32_t i=0; i<vt()->checker()->nbSecondaryTypes; i++) {
		J3Type* type = vt()->checker()->secondaryTypes[i]->type();

		if(type->isClass()) {
			J3Class* ifce = vt()->checker()->secondaryTypes[i]->type()->asClass();
			if(J3Cst::isInterface(ifce->access())) {
				//fprintf(stderr, "  processing interface: %s\n", ifce->name()->cStr());
				for(uint32_t j=0; j<ifce->nbMethods(); j++) {
					J3Method* base = ifce->methods()[j];
					//fprintf(stderr, "    processing %s method %s %s\n", 
					//J3Cst::isAbstract(base->access()) ? "abstract" : "concrete",
					//base->signature()->cStr(), base->name()->cStr());
					J3Method* method = findMethod(0, base->name(), base->signature(), J3Cst::isAbstract(base->access()));

					if(!method)
						method = base;

					uint32_t index = base->interfaceIndex() % J3VirtualTable::nbInterfaceMethodTable;
					uint32_t found = 0;

					for(uint32_t i=0; !found && i<slots[index].nbMethods; i++)
						if(slots[index].methods[i] == method)
							found=1;

					if(!found) {
						total++;
						J3Method** tmp = (J3Method**)alloca(sizeof(J3Method*)*(slots[index].nbMethods+1));
						memcpy(tmp, slots[index].methods, sizeof(J3Method*)*slots[index].nbMethods);
						tmp[slots[index].nbMethods] = method;
						slots[index].methods = tmp;
						slots[index].nbMethods++;
					}
				}
			}
		}
	}

	J3Method** methods = (J3Method**)loader()->allocator()->allocate(total*sizeof(J3Method*));
	uint32_t cur = 0;

	for(uint32_t i=0; i<J3VirtualTable::nbInterfaceMethodTable; i++) {
		memcpy(methods + cur, slots[i].methods, slots[i].nbMethods*sizeof(J3Method*));
		slots[i].methods = methods + cur;
		cur += slots[i].nbMethods;
	}

	//dumpInterfaceSlotDescriptors();
}

void J3ObjectType::dumpInterfaceSlotDescriptors() {
	J3InterfaceSlotDescriptor* slots = _interfaceSlotDescriptors;
	fprintf(stderr, "slot descriptors of %s\n", name()->cStr());
	for(uint32_t i=0; i<J3VirtualTable::nbInterfaceMethodTable; i++) {
		if(slots[i].nbMethods) {
			fprintf(stderr, "  slot[%d]:\n", i);
			for(uint32_t j=0; j<slots[i].nbMethods; j++)
				fprintf(stderr, "    %s::%s %s\n", 
								slots[i].methods[j]->cl()->name()->cStr(),
								slots[i].methods[j]->name()->cStr(),
								slots[i].methods[j]->signature()->name()->cStr());
		}
	}
}

/*  
 *  ------------ J3Layout ------------
 */
J3StaticLayout::J3StaticLayout(J3ClassLoader* loader, J3Class* cl, const vmkit::Name* name) : J3Layout(loader, name) {
	_cl = cl;
}

J3ObjectHandle* J3StaticLayout::extractAttribute(J3Attribute* attr) { 
	return cl()->extractAttribute(attr); 
}

J3Layout::J3Layout(J3ClassLoader* loader, const vmkit::Name* name) : J3ObjectType(loader, name) {
}

uintptr_t J3Layout::structSize() { 
	return _structSize; 
}

J3Method* J3Layout::localFindMethod(const vmkit::Name* name, J3Signature* signature) {
	//fprintf(stderr, " --- lookup %s%s in %s\n", name->cStr(), signature->name()->cStr(), J3Layout::name()->cStr());

	for(size_t i=0; i<nbMethods(); i++) {
		J3Method* cur = methods()[i];

		//fprintf(stderr, "%s::%s%s\n", cur->cl()->name()->cStr(), cur->name()->cStr(), cur->signature()->name()->cStr());

		if(cur->name() == name && cur->signature()->name() == signature->name()) {
			return cur;
		}
	}
	return 0;
}

J3Field* J3Layout::localFindField(const vmkit::Name* name, const J3Type* type) {
	for(size_t i=0; i<nbFields(); i++) {
		J3Field* cur = fields() + i;

		//printf("  compare with %s - %s\n", cur->name()->cStr(), cur->type()->name()->cStr());
		if(cur->name() == name && cur->type() == type) {
			return cur;
		}
	}
	return 0;
}

/*  
 *  ------------ J3Class ------------
 */
J3Class::J3Class(J3ClassLoader* loader, const vmkit::Name* name, J3ClassBytes* bytes, J3ObjectHandle* protectionDomain, const char* source) : 
	J3Layout(loader, name), 
	_staticLayout(loader, this, name) {
	_protectionDomain = protectionDomain ? loader->globalReferences()->add(protectionDomain) : 0;
	_source = source;
	_bytes = bytes;
	status = LOADED;
	_staticObjectSymbol = new(loader->staticObjects()) J3StaticObjectSymbol();
}

void J3Class::compileAll() {
	resolve();

	for(uint32_t i=0; i<nbMethods(); i++) {
		if(!J3Cst::isAbstract(methods()[i]->access()))
			methods()[i]->ensureCompiled(J3CodeGen::WithMethod);
	}

	for(uint32_t i=0; i<staticLayout()->nbMethods(); i++) {
		if(!J3Cst::isAbstract(staticLayout()->methods()[i]->access()))
			staticLayout()->methods()[i]->ensureCompiled(J3CodeGen::WithMethod);
	}
}

void J3Class::aotSnapshot(llvm::Linker* linker) {
	for(uint32_t i=0; i<nbMethods(); i++) {
		methods()[i]->aotSnapshot(linker);
	}

	for(uint32_t i=0; i<staticLayout()->nbMethods(); i++) {
		staticLayout()->methods()[i]->aotSnapshot(linker);
	}
}

uint16_t J3Class::modifiers() {
	return access();
#if 0
  if (isEnum(res) && cl->getSuper() != vm->upcalls->EnumClass) {
    // javac may put that flag to inner classes of enum classes.
    res &= ~ACC_ENUM;
  }
#endif
}

J3ObjectHandle* J3Class::clone(J3ObjectHandle* obj) {
	//fprintf(stderr, " cloning %p with %lu bytes\n", obj->obj(), structSize());
	J3ObjectHandle* res = J3ObjectHandle::doNewObject(this);
	obj->rawObjectCopyTo(0, res, 0, structSize() - sizeof(J3Object));
	return res;
}

J3ObjectHandle* J3Class::extractAttribute(J3Attribute* attr) {
	if(attr)
		J3::internalError("extract attribute");
	else
		return J3ObjectHandle::doNewArray(J3Thread::get()->vm()->typeByte->getArray(), 0);
}

J3Method* J3Class::findInterfaceMethodRecursive(const vmkit::Name* name, J3Signature* signature) {
	J3Class* cur = this;
	while(1) {
		J3Method* res = cur->localFindMethod(name, signature);

		if(res)
			return res;

		switch(cur->nbInterfaces()) {
			case 1: cur = cur->interfaces()[0]; break;
			default:
				for(uint32_t i=0; i<cur->nbInterfaces(); i++) {
					res = cur->interfaces()[i]->findInterfaceMethodRecursive(name, signature);
					if(res)
						return res;
				}
			case 0:
				return 0;
		}
	}
}

J3Method* J3Class::findInterfaceMethod(const vmkit::Name* name, J3Signature* signature, bool error) {
	resolve();
	J3Method* res = findInterfaceMethodRecursive(name, signature);

	if(res)
		return res;

	if(error)
		J3::noSuchMethodError("no such interface method", this, name, signature);
	else
		return 0;
}

J3Method* J3Class::findMethod(uint32_t access, const vmkit::Name* name, J3Signature* signature, bool error) {
	resolve();

	J3Class* cur = this;
	while(1) {
		J3Layout* layout = J3Cst::isStatic(access) ? (J3Layout*)cur->staticLayout() : cur;
		J3Method* res = layout->localFindMethod(name, signature);

		if(res)
			return res;

		if(cur == cur->super()) {
			if(error)
				J3::noSuchMethodError("no such method", this, name, signature);
			else
				return 0;
		}

		cur = cur->super();
	}
}

J3Field* J3Class::findInterfaceFieldRecursive(const vmkit::Name* name, J3Type* type) {
	J3Class* cur = this;

	while(1) {
		J3Field* res = cur->staticLayout()->localFindField(name, type);

		if(res)
			return res;

		switch(cur->nbInterfaces()) {
			case 1: cur = cur->interfaces()[0]; break;
			default:
				for(uint32_t i=0; i<cur->nbInterfaces(); i++) {
					res = cur->interfaces()[i]->findInterfaceFieldRecursive(name, type);
					if(res)
						return res;
				}
			case 0:
				return 0;
		}
	}
}

J3Field* J3Class::findField(uint32_t access, const vmkit::Name* name, J3Type* type, bool error) {
	resolve();

	J3Class* cur = this;

	while(1) {
		J3Layout* layout = J3Cst::isStatic(access) ? (J3Layout*)cur->staticLayout() : cur;
		J3Field* res = layout->localFindField(name, type);

		//fprintf(stderr, "[%d] Lookup: %s %s in %s\n", access, type->name()->cStr(), name->cStr(), cur->name()->cStr());

		if(res)
			return res;

		if(cur == cur->super()) {
			if(J3Cst::isStatic(access)) {
				J3Class* prev = 0;
				for(cur=this; cur!=prev; cur=cur->super()) {
					for(uint32_t i=0; i<cur->nbInterfaces(); i++) {
						res = cur->interfaces()[i]->findInterfaceFieldRecursive(name, type);
						if(res)
							return res;
					}
					prev = cur;
				}
			}

			if(error)
				J3::noSuchFieldError("no such field", this, name, type);
			else
				return 0;
		}

		cur = cur->super();
	}
}

void J3Class::registerNative(const vmkit::Name* name, const vmkit::Name* signatureName, void* fnPtr) {
	resolve();
	J3Signature* signature = loader()->getSignature(this, signatureName);
	J3Method* res = staticLayout()->localFindMethod(name, signature);
	if(!res)
		res = localFindMethod(name, signature);
	if(!res || !J3Cst::isNative(res->access()))
		J3::noSuchMethodError("unable to find native method", this, name, signature);

	res->registerNative(fnPtr);
}

char* J3Class::staticObjectId() {
	char* id = staticObjectSymbol()->id();

	if(!id) {
		size_t len = nativeNameLength();
		id = (char*)loader()->allocator()->allocate(len + 8);
		memcpy(id, "static_", 7);
		memcpy(id+7, nativeName(), len+1);
		staticObjectSymbol()->setId(id);
	}

	return id;
}

void J3Class::doInitialise() {
	resolve();
	lock();
	if(status < INITED) {
		J3* vm = J3Thread::get()->vm();
		if(vm->options()->debugIniting)
			fprintf(stderr, "Initing: %s\n", name()->cStr());
		status = INITED;

		super()->initialise();

		for(size_t i=0; i<nbInterfaces(); i++)
			interfaces()[i]->initialise();

		J3ObjectHandle* prev = J3Thread::get()->tell();
		J3ObjectHandle* stacked = J3ObjectHandle::allocate(staticLayout()->vt(), staticLayout()->structSize());

		staticObjectSymbol()->setHandle(stacked);
		J3Thread::get()->restore(prev);

		for(size_t i=0; i<staticLayout()->nbFields(); i++) {
			J3Field* cur = staticLayout()->fields() + i;
			J3Attribute* attr = cur->attributes()->lookup(vm->constantValueAttribute);

			if(attr) {
				J3Reader reader(bytes());
				reader.seek(attr->offset(), reader.SeekSet);

				uint32_t length = reader.readU4();
				if(length != 2)
					J3::classFormatError(this, "bad length for ConstantAttribute");
				
				uint32_t idx = reader.readU2();
				J3ObjectHandle* staticObject = staticObjectSymbol()->handle();

				switch(getCtpType(idx)) {
					case J3Cst::CONSTANT_Long:    staticObject->setLong(cur, longAt(idx)); break;
					case J3Cst::CONSTANT_Float:   staticObject->setFloat(cur, floatAt(idx)); break;
					case J3Cst::CONSTANT_Double:  staticObject->setDouble(cur, doubleAt(idx)); break;
					case J3Cst::CONSTANT_Integer: staticObject->setInteger(cur, integerAt(idx)); break;
					case J3Cst::CONSTANT_String:  staticObject->setObject(cur, stringAt(idx, 0)); break;
					default:
						J3::classFormatError(this, "invalid ctp entry ConstantAttribute with type %d", getCtpType(idx));
				}
			}
		}

		J3Method* clinit = staticLayout()->localFindMethod(vm->clinitName, vm->clinitSign);
			
		if(clinit)
			clinit->invokeStatic();
	}
	unlock();
}

void J3Class::doResolve(J3Field* hiddenFields, size_t nbHiddenFields) {
	lock();
	if(status < RESOLVED) {
		if(J3Thread::get()->vm()->options()->debugResolve)
			fprintf(stderr, "Resolving: %s\n", name()->cStr());

		status = RESOLVED;
		readClassBytes(hiddenFields, nbHiddenFields);
		
		staticLayout()->_vt = J3VirtualTable::create(staticLayout());

		_vt = J3VirtualTable::create(this);

		if(!J3Cst::isInterface(access()) && !J3Cst::isAbstract(access()))
			prepareInterfaceTable();
	}
	unlock();
}

void J3Class::readClassBytes(J3Field* hiddenFields, uint32_t nbHiddenFields) {
	J3Reader reader(_bytes);

	uint32_t magic = reader.readU4();
	if(magic != J3Cst::MAGIC)
		J3::classFormatError(this, "bad magic");
			
	/* uint16_t minor = */reader.readU2();
	/* uint16_t major = */reader.readU2();
			
	nbCtp     = reader.readU2();
	
	if(nbCtp < 1)
		J3::classFormatError(this, "zero-sized constant pool");
	
	ctpTypes    = (uint8_t*)loader()->allocator()->allocate(nbCtp * sizeof(uint8_t));
	ctpValues   = (uint32_t*)loader()->allocator()->allocate(nbCtp * sizeof(uint32_t));
	ctpResolved = (void**)loader()->allocator()->allocate(nbCtp * sizeof(void*));
	
	ctpTypes[0] = 0;

	for(uint32_t i=1; i<nbCtp; i++) {
		switch(ctpTypes[i] = reader.readU1()) {
			case J3Cst::CONSTANT_Utf8:
				ctpValues[i] = reader.tell();
				reader.seek(reader.readU2(), reader.SeekCur);
				break;
			case J3Cst::CONSTANT_MethodType:
			case J3Cst::CONSTANT_String:
			case J3Cst::CONSTANT_Class:
				ctpValues[i] = reader.readU2();
				break;
			case J3Cst::CONSTANT_InvokeDynamic:
			case J3Cst::CONSTANT_Float:
			case J3Cst::CONSTANT_Integer:
			case J3Cst::CONSTANT_Fieldref:
			case J3Cst::CONSTANT_Methodref:
			case J3Cst::CONSTANT_InterfaceMethodref:
			case J3Cst::CONSTANT_NameAndType:
				ctpValues[i] = reader.readU4();
				break;
			case J3Cst::CONSTANT_Long:
			case J3Cst::CONSTANT_Double:
				ctpValues[i] = reader.readU4();
				ctpValues[i+1] = reader.readU4();
				i++;
				break;
			case J3Cst::CONSTANT_MethodHandle:
				ctpValues[i] = reader.readU1() << 16;
				ctpValues[i] |= reader.readU2();
				break;
			default:
				J3::classFormatError(this, "wrong constant pool entry type: %d", ctpTypes[i]);
		}
	}
	
	_access = reader.readU2();
	
	J3ObjectType* self = classAt(reader.readU2());
	
	if(self != this)
		J3::classFormatError(this, "wrong class file (describes class %s)", self->name()->cStr());
	
	uint16_t superIdx = reader.readU2();

	_super = superIdx ? classAt(superIdx)->asClass() : this;

	_nbInterfaces = reader.readU2();
	_interfaces = (J3Class**)loader()->allocator()->allocate(nbInterfaces()*sizeof(J3Class*));
	
	for(size_t i=0; i<nbInterfaces(); i++) {
		_interfaces[i] = classAt(reader.readU2())->asClass();
	}

	size_t   n = nbHiddenFields + reader.readU2(), nbStaticFields = 0, nbVirtualFields = 0;
	_fields = (J3Field*)alloca(sizeof(J3Field)*n);
	J3Field* pFields0[n]; size_t i0 = 0; /* sort fields by reverse size */
	J3Field* pFields1[n]; size_t i1 = 0;
	J3Field* pFields2[n]; size_t i2 = 0;
	J3Field* pFields3[n]; size_t i3 = 0;

	memset(fields(), 0, sizeof(J3Field)*n);
	
	for(size_t i=0; i<n; i++) {
		J3Field* f = fields() + i;

		if(i < nbHiddenFields) {
			f->_access     = hiddenFields[i].access();
			f->_name       = hiddenFields[i].name();
			f->_type       = hiddenFields[i].type();
			f->_attributes = new (loader()->allocator(), 0) J3Attributes(0);
		} else {
			f->_access     = reader.readU2();
			f->_name       = nameAt(reader.readU2());
			f->_type       = loader()->getTypeFromDescriptor(this, nameAt(reader.readU2()));
			f->_attributes = readAttributes(&reader);
		}

		if(J3Cst::isStatic(f->access())) {
			f->_layout = staticLayout();
			nbStaticFields++;
		} else {
			f->_layout = this;
			nbVirtualFields++;
		}

		switch(f->_type->logSize()) {
			case 0:  pFields0[i0++] = f; break;
			case 1:  pFields1[i1++] = f; break;
			case 2:  pFields2[i2++] = f; break;
			case 3:  pFields3[i3++] = f; break;
			default: J3::internalError("should not happen");
		}
	}

	staticLayout()->_fields = new(loader()->allocator()) J3Field[nbStaticFields];
	_fields = new(loader()->allocator()) J3Field[nbVirtualFields];

	if(super() == this)
		_structSize = sizeof(J3Object);
	else {
		super()->resolve();
		_structSize = super()->structSize();
	}

	_staticLayout._structSize = sizeof(J3Object);
	_structSize = ((_structSize - 1) & -sizeof(uintptr_t)) + sizeof(uintptr_t);

	fillFields(pFields3, i3);
	fillFields(pFields2, i2);
	fillFields(pFields1, i1);
	fillFields(pFields0, i0);
	
	size_t     nbVirtualMethods = 0, nbStaticMethods = 0;

	n = reader.readU2();
	J3Method** methodsTmp = (J3Method**)alloca(sizeof(J3Method*)*n);

	for(size_t i=0; i<n; i++) {
		uint16_t           access = reader.readU2();
		const vmkit::Name* name = nameAt(reader.readU2());
		J3Signature*       signature = loader()->getSignature(this, nameAt(reader.readU2()));
		J3Method*          method = new(loader()->allocator()) J3Method(access, this, name, signature);
		J3Attributes*      attributes = readAttributes(&reader);
		
		method->postInitialise(access, attributes);
		methodsTmp[i] = method;

		if(J3Cst::isStatic(access))
			nbStaticMethods++;
		else
			nbVirtualMethods++;
	}

	staticLayout()->_methods = (J3Method**)loader()->allocator()->allocate(sizeof(J3Method*)*nbStaticMethods);
	_methods = (J3Method**)loader()->allocator()->allocate(sizeof(J3Method*)*nbVirtualMethods);

	for(int i=0; i<n; i++) {
		J3Layout* layout;
		if(J3Cst::isStatic(methodsTmp[i]->access()))
			layout = staticLayout();
		else {
			layout = this;
			if(methodsTmp[i]->name() == J3Thread::get()->vm()->initName) {
				_nbConstructors++;
				if(J3Cst::isPublic(methodsTmp[i]->access()))
					_nbPublicConstructors++;
			}
		}

		methodsTmp[i]->_slot = layout->_nbMethods;
		layout->_methods[layout->_nbMethods++] = methodsTmp[i];

		if(J3Cst::isPublic(methodsTmp[i]->access()))
			layout->_nbPublicMethods++;
	}

	_attributes = readAttributes(&reader);
}

void J3Class::fillFields(J3Field** fields, size_t n) {
	for(size_t i=0; i<n; i++) {
		J3Field*  cur = fields[i];
		J3Layout* layout = J3Cst::isStatic(fields[i]->access()) ? (J3Layout*)staticLayout() : this;

		//if(name() == J3Thread::get()->vm()->names()->get("java/lang/ClassLoader"))
		//fprintf(stderr, " field %s: %s\n", J3Cst::isStatic(fields[i]->access()) ? "static" : "virtual", fields[i]->name()->cStr());

		cur->_offset = layout->structSize();
		cur->_slot = layout->_nbFields;
		layout->_structSize += 1 << fields[i]->type()->logSize();
		layout->fields()[layout->_nbFields++] = *fields[i];

		if(J3Cst::isPublic(fields[i]->access()))
			layout->_nbPublicFields++;
	}
}

J3Attributes* J3Class::readAttributes(J3Reader* reader) {
	size_t nbAttributes = reader->readU2();
	J3Attributes* res = new (loader()->allocator(), nbAttributes) J3Attributes(nbAttributes);
	
	for(size_t i=0; i<nbAttributes; i++) {
		res->attribute(i)->_id     = nameAt(reader->readU2());
		res->attribute(i)->_offset = reader->tell();
		reader->seek(reader->readU4(), reader->SeekCur);
	}

	return res;
}

uint8_t J3Class::getCtpType(uint16_t idx) {
	check(idx);
	return ctpTypes[idx];
}

void* J3Class::getCtpResolved(uint16_t idx) {
	check(idx);
	return ctpResolved[idx];
}

J3ObjectHandle* J3Class::stringAt(uint16_t idx, bool doPush) {
	check(idx, J3Cst::CONSTANT_String);
	J3ObjectHandle* res = (J3ObjectHandle*)ctpResolved[idx];
	if(!res) {
		ctpResolved[idx] = res = J3Thread::get()->vm()->nameToString(nameAt(ctpValues[idx]), 0);
	}
	return (J3ObjectHandle*)res;
}

float J3Class::floatAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Float);
	J3Value v;
	v.valInteger = ctpValues[idx];
	return v.valFloat;
}

double J3Class::doubleAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Double);
	J3Value v;
	v.valLong = ((uint64_t)ctpValues[idx] << 32) + (uint64_t)ctpValues[idx+1];
	return v.valDouble;
}

uint32_t J3Class::integerAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Integer);
	return ctpValues[idx];
}

uint64_t J3Class::longAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Long);
	return ((uint64_t)ctpValues[idx] << 32) + (uint64_t)ctpValues[idx+1];
}

J3Method* J3Class::interfaceOrMethodAt(uint16_t idx, uint16_t access, bool isInterfaceMethod) {
	J3Method* res = (J3Method*)ctpResolved[idx];
	
	if(res) {
		if((res->access() & J3Cst::ACC_STATIC) != (access & J3Cst::ACC_STATIC))
			J3::classFormatError(this, "inconsistent use of virtual and static methods"); 
		return res;
	}

	uint16_t ntIdx = ctpValues[idx] & 0xffff;
	J3ObjectType* cl = classAt(ctpValues[idx] >> 16);

	check(ntIdx, J3Cst::CONSTANT_NameAndType);

	const vmkit::Name* name = nameAt(ctpValues[ntIdx] >> 16);
	J3Signature* signature = (J3Signature*)ctpResolved[ntIdx];
	if(!signature)
		ctpResolved[idx] = signature = loader()->getSignature(this, nameAt(ctpValues[ntIdx] & 0xffff));

	res = (isInterfaceMethod && J3Cst::isInterface(cl->access())) ? 
		cl->asClass()->findInterfaceMethod(name, signature) : 
		cl->findMethod(access, name, signature);

	ctpResolved[idx] = res;

	return res;
}

J3Method* J3Class::methodAt(uint16_t idx, uint16_t access) {
	check(idx, J3Cst::CONSTANT_Methodref);
	return interfaceOrMethodAt(idx, access, 0);
}

J3Method* J3Class::interfaceMethodAt(uint16_t idx, uint16_t access) {
	check(idx, J3Cst::CONSTANT_InterfaceMethodref);
	return interfaceOrMethodAt(idx, access, 1);
}

J3Field* J3Class::fieldAt(uint16_t idx, uint16_t access) {
	check(idx, J3Cst::CONSTANT_Fieldref);
	J3Field* res = (J3Field*)ctpResolved[idx];

	if(res) {
		if((res->access() & J3Cst::ACC_STATIC) != (access & J3Cst::ACC_STATIC))
			J3::classFormatError(this, "inconstitent use of virtual and static field"); 
		return res;
	}

	uint16_t ntIdx = ctpValues[idx] & 0xffff;
	J3Class* cl = classAt(ctpValues[idx] >> 16)->asClass();

	check(ntIdx, J3Cst::CONSTANT_NameAndType);
	const vmkit::Name* name = nameAt(ctpValues[ntIdx] >> 16);
	J3Type*            type = (J3Type*)ctpResolved[ntIdx];

	if(!type)
		ctpResolved[ntIdx] = type = loader()->getTypeFromDescriptor(this, nameAt(ctpValues[ntIdx] & 0xffff));
	
	res = cl->findField(access, name, type);

	return res;
}

J3ObjectType* J3Class::classAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Class);
	J3ObjectType* res = (J3ObjectType*)ctpResolved[idx];

	if(res)
		return res;

	const char* buf;
	size_t length; 

	utfAt(ctpValues[idx], &buf, &length);

	res = loader()->getTypeFromQualified(this, buf, length);

	ctpResolved[idx] = res;

	return res;
}

void J3Class::utfAt(uint16_t idx, const char** buf, size_t* length) {
	check(idx, J3Cst::CONSTANT_Utf8);

	J3Reader reader(_bytes);

	reader.seek(ctpValues[idx], reader.SeekSet);

	*length = reader.readU2();
	*buf = (const char*)reader.pointer();
}

const vmkit::Name*  J3Class::nameAt(uint16_t idx) {
	check(idx, J3Cst::CONSTANT_Utf8);
	const vmkit::Name* res = (const vmkit::Name*)ctpResolved[idx];

	if(res)
		return res;

	const char* buf;
	size_t length;
	utfAt(idx, &buf, &length);

	res = J3Thread::get()->vm()->names()->get(buf, 0, length);//(const char*)reader.pointer(), 0, len);

	ctpResolved[idx] = (void*)res;

	return res;
}

void J3Class::check(uint16_t idx, uint32_t id) {
	if(idx > nbCtp || (id != -1 && ctpTypes[idx] != id))
		J3::classFormatError(this, "wrong constant pool type %d at index %d for %d", id, idx, nbCtp);
}

void J3Class::doNativeName() {
	J3Mangler mangler(this);

	mangler.mangle(name());
		
	_nativeNameLength = mangler.length() + 3;
	_nativeName = (char*)loader()->allocator()->allocate(_nativeNameLength + 1);

	_nativeName[0] = 'L';
	memcpy(_nativeName + 1, mangler.cStr(), mangler.length());
	_nativeName[_nativeNameLength-2] = '_';
	_nativeName[_nativeNameLength-1] = '2';
	_nativeName[_nativeNameLength]   = 0;
}

/*  
 *  ------------ J3ArrayClass ------------
 */
J3ArrayClass::J3ArrayClass(J3ClassLoader* loader, J3Type* component, const vmkit::Name* name) : J3ObjectType(loader, name) {
	_component = component;

	if(!name) {
		const vmkit::Name* compName = component->name();
		uint32_t           len = compName->length();
		char               buf[len + 16];
		uint32_t           pos = 0;

		//printf("     build array of %s\n", component->name()->cStr());
		buf[pos++] = J3Cst::ID_Array;
	
		if(component->isClass())
			buf[pos++] = J3Cst::ID_Classname;
		memcpy(buf+pos, compName->cStr(), len * sizeof(char));
		pos += len;
		if(component->isClass())
			buf[pos++] = J3Cst::ID_End;
		buf[pos] = 0;

		_name = J3Thread::get()->vm()->names()->get(buf);
	}
}

J3ObjectHandle* J3ArrayClass::clone(J3ObjectHandle* obj) {
	size_t n = obj->arrayLength();
	J3ObjectHandle* res = J3ObjectHandle::doNewArray(this, n);
	obj->rawArrayCopyTo(0, res, 0, n<<component()->logSize());
	return res;
}

uint16_t J3ArrayClass::access() {
	return super()->access();
}

uint16_t J3ArrayClass::modifiers() {
	return super()->modifiers();
}

J3Class* J3ArrayClass::super() {
	return J3Thread::get()->vm()->objectClass;
}

J3Method* J3ArrayClass::findMethod(uint32_t access, const vmkit::Name* name, J3Signature* signature, bool error) {
	return super()->findMethod(access, name, signature, error);
}

void J3ArrayClass::doResolve(J3Field* hiddenFields, size_t nbHiddenFields) {
	lock();
	if(status < RESOLVED) {
		status = RESOLVED;
		_vt = J3VirtualTable::create(this);
		prepareInterfaceTable();
	}
	unlock();
}
	
void J3ArrayClass::doInitialise() {
	resolve();
	status = INITED;
}

void J3ArrayClass::doNativeName() {
	uint32_t len = component()->nativeNameLength();

	_nativeNameLength = len + 2;
	_nativeName = (char*)loader()->allocator()->allocate(_nativeNameLength + 1);

	_nativeName[0] = '_';
	_nativeName[1] = '3';

	memcpy(_nativeName+2, component()->nativeName(), len);
	_nativeName[_nativeNameLength] = 0;
}

J3ObjectHandle* J3ArrayClass::multianewArray(uint32_t dim, uint32_t* args) {
	J3ObjectHandle* res = J3ObjectHandle::doNewArray(this, args[0]);

	if(dim > 1)
		for(uint32_t i=0; i<args[0]; i++)
			res->setObjectAt(i, component()->asArrayClass()->multianewArray(dim-1, args+1));

	return res;
}

/*  
 *  ------------ J3Primitive ------------
 */
J3Primitive::J3Primitive(J3ClassLoader* loader, char id, llvm::Type* type, uint32_t logSize) : 
	J3Type(loader, J3Thread::get()->vm()->names()->get(id)) {
	_llvmType = type;
	_nativeName = (char*)loader->allocator()->allocate(2);
	_nativeName[0] = id;
	_nativeName[1] = 0;
	_nativeNameLength = 1;
	_vt = J3VirtualTable::create(this);
	_logSize = logSize;
}

void J3Primitive::defineJavaClass(const char* className) {
	J3* vm = J3Thread::get()->vm();
	_javaClass = vm->initialClassLoader->loadClass(vm->names()->get(className))->javaClass(0);
}
