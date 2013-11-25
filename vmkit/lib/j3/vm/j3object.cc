#include <stdint.h>

#include "llvm/IR/DataLayout.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include "vmkit/allocator.h"
#include "vmkit/gc.h"
#include "j3/j3object.h"
#include "j3/j3method.h"
#include "j3/j3class.h"
#include "j3/j3classloader.h"
#include "j3/j3.h"
#include "j3/j3constants.h"
#include "j3/j3thread.h"

using namespace j3;

/*
 *    ---   J3TypeChecker ---
 */
void J3TypeChecker::dump() {
	fprintf(stderr, "    offset: %u\n", offset);
	for(uint32_t i=0; i<cacheOffset; i++) {
		if(display[i])
			fprintf(stderr, "    display[%u]: %ls\n", i, display[i]->type()->name()->cStr());
	}
	for(uint32_t i=0; i<nbSecondaryTypes; i++) {
		fprintf(stderr, "    secondary[%u]: %ls\n", i, secondaryTypes[i]->type()->name()->cStr());
	}
	if(display[cacheOffset])
		fprintf(stderr, "    cache: %ls\n", display[cacheOffset]->type()->name()->cStr());
}

/*
 *    ---   J3VirtualTable ---
 */
J3VirtualTable* J3VirtualTable::create(J3Layout* layout) {
	return new(layout->loader()->allocator(), 0) J3VirtualTable(layout, layout, 0, 0, 0);
}

J3VirtualTable* J3VirtualTable::create(J3Class* cl) {
	J3Class* super = cl->super();
	uint32_t base = cl == super ? 0 : super->vt()->nbVirtualMethods();
	uint32_t n = base;

	super->resolve();

	J3Method* pm[cl->nbMethods()];

	for(uint32_t i=0; i<cl->nbMethods(); i++) {
		J3Method* meth = cl->methods()[i];
		J3Method* parent = cl == super ? 0 : super->findVirtualMethod(meth->name(), meth->sign(), 0);
		
		if(parent) {
			pm[i] = parent;
			meth->setResolved(parent->index());
		} else {
			pm[i] = meth;
			meth->setResolved(n);
			n++;
		}
	}

	J3VirtualTable* res = new(cl->loader()->allocator(), n) 
		J3VirtualTable(cl, cl->super(), (J3Type**)cl->interfaces(), cl->nbInterfaces(), J3Cst::isInterface(cl->access()) ? 1 : 0);


	/* virtual table */
	res->_nbVirtualMethods = n;
	if(super != cl)  /* super->vt() is not yet allocated for Object */
		memcpy(res->_virtualMethods, super->vt()->_virtualMethods, sizeof(void*)*super->vt()->nbVirtualMethods());

	for(uint32_t i=0; i<cl->nbMethods(); i++) 
		res->_virtualMethods[pm[i]->index()] = pm[i]->functionPointerOrTrampoline();

	return res;
}

J3VirtualTable* J3VirtualTable::create(J3ArrayClass* cl) {
	J3Class* objClass           = cl->loader()->vm()->objectClass;
	J3Type* super               = cl->component();
	J3Type* base                = super;
	uint32_t dim                = 1;
	J3Type** secondaries        = 0;
	uint32_t nbSecondaries      = 0;
	bool isSecondary            = 0;

	// for objects                      for primitives    
	// Integer[][]                                   
	// Number[][]  + ifces[][]                       int[][][]
	// Object[][]                                    Object[][]
	// Object[]                    int[]             Object[]
	//            Object + Serializable/Cloneable

	while(base->isArrayClass()) {
		base = base->asArrayClass()->component();
		dim++;
	}

	if(base->isPrimitive()) {
		super = objClass->getArray(dim-1);
		nbSecondaries = cl->loader()->vm()->nbArrayInterfaces;
		secondaries = (J3Type**)cl->loader()->allocator()->allocate(nbSecondaries*sizeof(J3Type*));
		for(uint32_t i=0; i<nbSecondaries; i++) {
			secondaries[i] = cl->loader()->vm()->arrayInterfaces[i];
			if(dim > 1)
				secondaries[i] = secondaries[i]->getArray(dim-1);
		}
	} else if(base == objClass) {
		nbSecondaries = cl->loader()->vm()->nbArrayInterfaces;
		secondaries = (J3Type**)alloca(nbSecondaries*sizeof(J3Type*));
		for(uint32_t i=0; i<nbSecondaries; i++) {
			secondaries[i] = cl->loader()->vm()->arrayInterfaces[i];
			if(dim > 1)
				secondaries[i] = secondaries[i]->getArray(dim - 1);
		}
	} else {
		J3Class* baseClass = base->asClass();
		baseClass->resolve();
		if(J3Cst::isInterface(baseClass->access()))
			isSecondary = 1;
		super = baseClass->super()->getArray(dim);
		super->resolve();
		//printf("%ls super is %ls (%d)\n", cl->name()->cStr(), super->name()->cStr(), isSecondary);

		uint32_t n = baseClass->vt()->checker.nbSecondaryTypes;
		secondaries = (J3Type**)alloca(n*sizeof(J3Type*));

		for(uint32_t i=0; i<n; i++) {
			secondaries[nbSecondaries] = baseClass->vt()->checker.secondaryTypes[i]->type();
			if(secondaries[i] != baseClass) { /* don't add myself */
				secondaries[nbSecondaries] = secondaries[nbSecondaries]->getArray(dim);
				nbSecondaries++;
			}
		}
	}

	super->resolve();

	J3VirtualTable* res = new(cl->loader()->allocator(), 0) J3VirtualTable(cl, super, secondaries, nbSecondaries, isSecondary);
	//memcpy(_virtualMethods, objClass->vt()->virtualMethods(), sizeof(void*)*objClass->nbVt());

	return res;
}

J3VirtualTable* J3VirtualTable::create(J3Primitive* prim) {
	return new(prim->loader()->allocator(), 0) J3VirtualTable(prim, prim, 0, 0, 0);
}

void* J3VirtualTable::operator new(size_t unused, vmkit::BumpAllocator* allocator, size_t n) {
	return allocator->allocate(sizeof(J3VirtualTable) + n*sizeof(void*) - sizeof(void*));
}

J3VirtualTable::J3VirtualTable(J3Type* type, J3Type* super, J3Type** interfaces, uint32_t nbInterfaces, bool isSecondary) {
	_type = type;

	//printf("***   Building the vt of %ls based on %ls\n", type->name()->cStr(), super->name()->cStr());

	if(super == type) {
		checker.offset = 0;
		checker.display[checker.offset] = this;
		if(nbInterfaces)
			J3::internalError(L"a root J3VirtualTable should not have interfaces");
	} else {
		uint32_t parentDisplayLength = super->vt()->checker.offset + 1;

		//printf("%ls (%p) secondary: %p %p (%d)\n", type->name()->cStr(), this, checker.secondaryTypes, checker.display[6], parentDisplayLength);

		if(parentDisplayLength >= J3TypeChecker::cacheOffset)
			isSecondary = 1;

		memcpy(checker.display, super->vt()->checker.display, parentDisplayLength*sizeof(J3VirtualTable*));

		checker.nbSecondaryTypes = super->vt()->checker.nbSecondaryTypes + nbInterfaces + isSecondary;
		checker.secondaryTypes = (J3VirtualTable**)super->loader()->allocator()->allocate(checker.nbSecondaryTypes*sizeof(J3VirtualTable*));
		
		//printf("%ls: %d - %d %d\n", type->name()->cStr(), isSecondary, parentDisplayLength, J3TypeChecker::displayLength);
		if(isSecondary) {
			checker.offset = J3TypeChecker::cacheOffset;
			checker.secondaryTypes[0] = this;
		} else {
			checker.offset = parentDisplayLength;
			checker.display[checker.offset] = this;
		} 

		for(uint32_t i=0; i<nbInterfaces; i++) {
			J3Type* sec = interfaces[i];
			//printf("In %ls - adding %ls at %d\n", type->name()->cStr(), sec->name()->cStr(), isSecondary+i);
			sec->resolve();
			checker.secondaryTypes[isSecondary+i] = sec->vt();
		}
		
		memcpy(checker.secondaryTypes + nbInterfaces + isSecondary, 
					 super->vt()->checker.secondaryTypes, 
					 super->vt()->checker.nbSecondaryTypes*sizeof(J3VirtualTable*));
	}

	//dump();
}

bool J3VirtualTable::slowIsAssignableTo(J3VirtualTable* parent) {
	for(uint32_t i=0; i<checker.nbSecondaryTypes; i++)
		if(checker.secondaryTypes[i] == parent) {
			checker.display[J3TypeChecker::cacheOffset] = parent;
			return true;
		}
	return false;
}

bool J3VirtualTable::fastIsAssignableToPrimaryChecker(J3VirtualTable* parent, uint32_t parentOffset) {
	return checker.display[parentOffset] == parent;
}

bool J3VirtualTable::fastIsAssignableToNonPrimaryChecker(J3VirtualTable* parent) {
	if(checker.display[J3TypeChecker::cacheOffset] == parent)
		return true;
	else if(parent == this)
		return true;
	else 
		return slowIsAssignableTo(parent);
}

bool J3VirtualTable::isAssignableTo(J3VirtualTable* parent) {
	uint32_t parentOffset = parent->checker.offset;
	if(checker.display[parentOffset] == parent)
		return true;
	else if(parentOffset != J3TypeChecker::cacheOffset)
		return false;
	else if(parent == this)
		return true;
	else
		return slowIsAssignableTo(parent);
}

void J3VirtualTable::dump() {
	fprintf(stderr, "VirtualTable: %s%ls (%p)\n", 
					type()->isLayout() && !type()->isClass() ? "static_" : "",
					type()->name()->cStr(), this);
	checker.dump();

}

/*
 *    ---   J3Object ---
 */
J3VirtualTable* J3Object::vt() { 
	return _vt; 
}

uintptr_t* J3Object::header() { 
	return &_header; 
}

J3Object* J3Object::allocate(J3VirtualTable* vt, size_t n) {
	J3Object* res = (J3Object*)vmkit::GC::allocate(n);
	res->_vt = vt;
	return res;
}

J3Object* J3Object::doNewNoInit(J3Class* cl) {
	return allocate(cl->vt(), cl->size());
}

J3Object* J3Object::doNew(J3Class* cl) {
	cl->initialise();
	return doNewNoInit(cl);
}

/*
 *    ---   J3ArrayObject ---
 */
J3Object* J3ArrayObject::doNew(J3ArrayClass* cl, uint32_t length) {
	llvm::DataLayout* layout = cl->loader()->vm()->dataLayout();
	J3ArrayObject* res = 
		(J3ArrayObject*)allocate(cl->vt(),
														 layout->getTypeAllocSize(cl->llvmType()->getContainedType(0))
														 + layout->getTypeAllocSize(cl->component()->llvmType()) * length);

	res->_length = length;

	return res;
}

/*
 *    J3ObjectHandle
 */
uint32_t J3ObjectHandle::hashCode() {
	do {
		uintptr_t oh = *obj()->header();
		uintptr_t res = oh;
		if(res)
			return res;
		uintptr_t nh = oh + 256;
		__sync_val_compare_and_swap(obj()->header(), oh, nh);
	} while(1);
}

J3ObjectHandle* J3ObjectHandle::allocate(J3VirtualTable* vt, size_t n) {
	J3Object* res = J3Object::allocate(vt, n);
	return J3Thread::get()->push(res);
}

J3ObjectHandle* J3ObjectHandle::doNewObject(J3Class* cl) {
	J3Object* res = J3Object::doNew(cl);
	return J3Thread::get()->push(res);
}

J3ObjectHandle* J3ObjectHandle::doNewArray(J3ArrayClass* cl, uint32_t length) {
	J3Object* res = J3ArrayObject::doNew(cl, length);
	return J3Thread::get()->push(res);
}



#define defAccessor(name, ctype, llvmtype)															\
	void J3ObjectHandle::rawSet##name(uint32_t offset, ctype value) {			\
		*((ctype*)((uintptr_t)obj() + offset)) = value;											\
	}																																			\
																																				\
	ctype J3ObjectHandle::rawGet##name(uint32_t offset) {									\
		return *((ctype*)((uintptr_t)obj() + offset));											\
	}																																			\
																																				\
	void J3ObjectHandle::set##name(J3Field* field, ctype value) {					\
		const llvm::StructLayout* layout =																	\
			obj()->vt()->type()->loader()->vm()->dataLayout()->								\
			getStructLayout((llvm::StructType*)(obj()->vt()->type()->llvmType()->getContainedType(0))); \
		uint32_t offset = layout->getElementOffset(field->num());						\
		rawSet##name(offset, value);																				\
	}																																			\
																																				\
	ctype J3ObjectHandle::get##name(J3Field* field) {											\
		const llvm::StructLayout* layout =																	\
			obj()->vt()->type()->loader()->vm()->dataLayout()->								\
			getStructLayout((llvm::StructType*)(obj()->vt()->type()->llvmType()->getContainedType(0))); \
		uint32_t offset = layout->getElementOffset(field->num());						\
		return rawGet##name(offset);																				\
	}																																			\
																																				\
	void J3ObjectHandle::set##name##At(uint32_t idx, ctype value) {				\
		rawSet##name(sizeof(J3ArrayObject) + idx*sizeof(ctype), value);			\
	}																																			\
																																				\
	ctype J3ObjectHandle::get##name##At(uint32_t idx) {										\
		return rawGet##name(sizeof(J3ArrayObject) + idx*sizeof(ctype));			\
	}

onJavaPrimitives(defAccessor)

#undef defAccessor

void J3ObjectHandle::rawSetObject(uint32_t offset, J3ObjectHandle* value) {
	*((J3Object**)((uintptr_t)obj() + offset)) = value->obj();
}

J3ObjectHandle* J3ObjectHandle::rawGetObject(uint32_t offset) {
	return J3Thread::get()->push(*((J3Object**)((uintptr_t)obj() + offset)));
}

void J3ObjectHandle::setObject(J3Field* field, J3ObjectHandle* value) {
	const llvm::StructLayout* layout =
		obj()->vt()->type()->loader()->vm()->dataLayout()->
		getStructLayout((llvm::StructType*)(obj()->vt()->type()->llvmType()->getContainedType(0)));
	uint32_t offset = layout->getElementOffset(field->num());
	rawSetObject(offset, value);
}

J3ObjectHandle* J3ObjectHandle::getObject(J3Field* field) {
	const llvm::StructLayout* layout =
		obj()->vt()->type()->loader()->vm()->dataLayout()->
		getStructLayout((llvm::StructType*)(obj()->vt()->type()->llvmType()->getContainedType(0)));
	return rawGetObject(layout->getElementOffset(field->num()));
}

void J3ObjectHandle::setObjectAt(uint32_t idx, J3ObjectHandle* value) {
	rawSetObject(sizeof(J3ArrayObject) + idx*sizeof(J3Object*), value);
}

J3ObjectHandle* J3ObjectHandle::getObjectAt(uint32_t idx) {
	return rawGetObject(sizeof(J3ArrayObject) + idx*sizeof(J3Object*));
}

void* J3ObjectHandle::trampoline(J3Object* obj, J3Method* target) {
	J3ObjectHandle* prev = J3Thread::get()->tell();
	J3ObjectHandle* handle = J3Thread::get()->push(obj);

	void* res = target->fnPtr();

	if(!J3Cst::isStatic(target->access()))
		handle->vt()->virtualMethods()[target->index()] = res;

	return res;
}

/*
 *  J3FixedPoint
 */
J3FixedPoint::J3FixedPoint(vmkit::BumpAllocator* _allocator) {
	pthread_mutex_init(&mutex, 0);
	allocator = _allocator;
	head = 0;
	createNode();
}

void J3FixedPoint::unsyncEnsureCapacity(uint32_t capacity) {
	J3ObjectHandle* reserve = head->top + capacity;
	if(reserve > head->max)
		createNode(capacity);
}

void J3FixedPoint::createNode(uint32_t capacity) {
	uint64_t size = capacity * sizeof(J3Object*) + sizeof(J3FixedPointNode);
	J3FixedPointNode* nn = (J3FixedPointNode*)allocator->allocate(size);
	nn->top = (J3ObjectHandle*)(nn + 1);
	nn->max = (J3ObjectHandle*)((uintptr_t)nn + size);
	nn->nextFree = 0;
	nn->nextBusy = head;
	if(head)
		head->nextFree = nn;
	head = nn;
}

J3ObjectHandle* J3FixedPoint::syncPush(J3Object* obj) {
	J3FixedPointNode* cur = head;
	J3ObjectHandle* res = (J3ObjectHandle*)__sync_fetch_and_add((uintptr_t*)&cur->top, (uintptr_t)sizeof(J3ObjectHandle));

	if(res >= cur->max) {
		pthread_mutex_lock(&mutex);
		if(cur->nextFree)
			head = cur->nextFree;
		else
			createNode();
		pthread_mutex_unlock(&mutex);
		return syncPush(obj);
	} else {
		res->_obj = obj;
		return res;
	}
}

J3ObjectHandle* J3FixedPoint::unsyncPush(J3Object* obj) {
	J3ObjectHandle* res = head->top++;

	if(res >= head->max) {
		if(head->nextFree)
			head = head->nextFree;
		else
			createNode();
		return unsyncPush(obj);
	} else {
		res->_obj = obj;
		return res;
	}
}

void J3FixedPoint::unsyncPop() {
	J3ObjectHandle* res = head->top - 1;
	if(res < (J3ObjectHandle*)(head + 1)) {
		head = head->nextBusy;
		head->top = (J3ObjectHandle*)(head+1);		
	} else
		head->top = res;
}

void J3FixedPoint::unsyncRestore(J3ObjectHandle* obj) {
	while(obj <= (J3ObjectHandle*)head || obj > head->max) {
		head = head->nextBusy;
		head->top = (J3ObjectHandle*)(head+1);
	}
	head->top = obj;
}


