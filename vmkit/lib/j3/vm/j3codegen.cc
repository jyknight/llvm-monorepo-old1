#include <stdio.h>

#include "j3/j3.h"
#include "j3/j3thread.h"
#include "j3/j3reader.h"
#include "j3/j3codegen.h"
#include "j3/j3class.h"
#include "j3/j3constants.h"
#include "j3/j3classloader.h"
#include "j3/j3method.h"
#include "j3/j3mangler.h"
#include "j3/j3jni.h"
#include "j3/j3object.h"
#include "j3/j3field.h"
#include "j3/j3attribute.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Intrinsics.h"

#include "llvm/DebugInfo.h"
#include "llvm/DIBuilder.h"

using namespace j3;

#define _onEndPoint() ({ if(onEndPoint()) return; })

J3CodeGen::J3CodeGen(vmkit::BumpAllocator* _allocator, J3Method* m, bool withMethod, bool withCaller, bool onlyTranslate) :
	builder(J3Thread::get()->vm()->llvmContext()),
	exceptions(this) {

	allocator = _allocator;

	method = m;
	cl = method->cl()->asClass();
	signature = method->signature();
	loader = cl->loader();
	vm = J3Thread::get()->vm();

#if 0
	/* usefull to debug a single function */
	if(   cl->name() == vm->names()->get("sun/util/calendar/BaseCalendar") &&
				method->name() == vm->names()->get("getFixedDate") &&
				method->signature()->name() == vm->names()->get("(IIILsun/util/calendar/BaseCalendar$Date;)J") ) {

		vm->options()->debugTranslate = 3;
	}
#endif

	if(vm->options()->debugTranslate)
		fprintf(stderr, "  translating bytecode of: %s::%s%s\n", 
						cl->name()->cStr(), 
						method->name()->cStr(), 
						method->signature()->name()->cStr());

	module = new llvm::Module(method->llvmFunctionName(), builder.getContext());
	llvmFunction = buildFunction(method, 0);
	llvmFunction->setGC("vmkit");

	bbCheckCastFailed = 0;
	bbNullCheckFailed = 0;
	topPendingBranchs = 0;
	isWide = 0;

	uintPtrTy = vm->dataLayout()->getIntPtrType(module->getContext());
	nullValue = llvm::ConstantPointerNull::get((llvm::PointerType*)vm->typeJ3ObjectPtr);

#define _x(name, id)														\
	name = vm->introspectFunction(module, id);
#include "j3/j3meta.def"
#undef _x

	gvTypeInfo               = vm->introspectGlobalValue(module,  "typeinfo for void*");

	gcRoot                   = vm->getGCRoot(module);

	frameAddress             = llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::frameaddress);

#if 0
	//stackMap       = llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::experimental_stackmap);
	//patchPointVoid = llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::experimental_patchpoint_i64);
	{
		llvm::Type* ins[] = {
			builder.getInt64Ty(),
			builder.getInt32Ty(),
			builder.getInt8PtrTy(),
			builder.getInt32Ty()
		};
		patchPointVoid = (llvm::Function*)
			module->getOrInsertFunction(llvm::Intrinsic::getName(llvm::Intrinsic::experimental_patchpoint_void),
																		llvm::FunctionType::get(builder.getVoidTy(), ins, 1));
	}
#endif

	if(withMethod) {
		if(J3Cst::isNative(method->access()))
			generateNative();
		else
			generateJava();

		if(vm->options()->debugTranslate > 2)
			llvmFunction->dump();
	}

	uint32_t access = method->access();
	uint32_t needsCaller = withCaller && !signature->caller(access);
	if(needsCaller)
		signature->generateCallerIR(access, this, module, "generic-caller");

	if(!onlyTranslate)
		loader->compileModule(module);
	
	if(needsCaller)
		signature->setCaller(access, (J3Signature::function_t)loader->ee()->getFunctionAddress("generic-caller"));

	if(withMethod) {
		void* fnPtr = onlyTranslate ? 0 : (void*)loader->ee()->getFunctionAddress(llvmFunction->getName().data());
		method->markCompiled(llvmFunction, fnPtr);
	}
}

J3CodeGen::~J3CodeGen() {
}

void* J3CodeGen::operator new(size_t n, vmkit::BumpAllocator* _allocator) {
	return _allocator->allocate(n);
}

void J3CodeGen::operator delete(void* ptr) {
}

void J3CodeGen::translate(J3Method* method, bool withMethod, bool withCaller, bool onlyTranslate) {
	J3Thread::get()->vm()->lockCompiler();
	
	vmkit::BumpAllocator* allocator = vmkit::BumpAllocator::create();
	delete new(allocator) J3CodeGen(allocator, method, withMethod, withCaller, onlyTranslate);
	vmkit::BumpAllocator::destroy(allocator);

	J3Thread::get()->vm()->unlockCompiler();
}

uint32_t J3CodeGen::wideReadU1() {
	if(isWide) {
		isWide = 0;
		return codeReader->readU2();
	} else
		return codeReader->readU1();
}

uint32_t J3CodeGen::wideReadS1() {
	if(isWide) {
		isWide = 0;
		return codeReader->readS2();
	} else
		return codeReader->readS1();
}

llvm::Value* J3CodeGen::flatten(llvm::Value* v) {
	llvm::Type* type = v->getType();

	if(type == vm->typeInteger->llvmType() || type == vm->typeLong->llvmType() || 
		 type == vm->typeFloat->llvmType() || type == vm->typeDouble->llvmType() ||
		 (type->isPointerTy() && (v->getType() == vm->typeJ3ObjectPtr)))
		return v;
	else if(type == vm->typeBoolean->llvmType() || type == vm->typeByte->llvmType() || type == vm->typeShort->llvmType())
		return builder.CreateSExt(v, vm->typeInteger->llvmType());
	else if(type == vm->typeCharacter->llvmType())
		return builder.CreateZExt(v, vm->typeInteger->llvmType());

	fprintf(stderr, " v: ");
	v->getType()->dump();
	fprintf(stderr, "\n type: ");
	type->dump();
	fprintf(stderr, "\n");
	J3::internalError("should not happen");
}

llvm::Value* J3CodeGen::unflatten(llvm::Value* v, llvm::Type* type) {
	if(type == vm->typeInteger->llvmType() || type == vm->typeLong->llvmType() || 
		 type == vm->typeFloat->llvmType() || type == vm->typeDouble->llvmType() ||
		 (type->isPointerTy() && type == v->getType()))
		return v;
	else if(type == vm->typeBoolean->llvmType() || type == vm->typeByte->llvmType() || type == vm->typeShort->llvmType())
		return builder.CreateSExtOrTrunc(v, type);
	else if(type == vm->typeCharacter->llvmType())
		return builder.CreateZExtOrTrunc(v, type);

	fprintf(stderr, " v: ");
	v->getType()->dump();
	fprintf(stderr, "\n type: ");
	type->dump();
	fprintf(stderr, "\n");
	J3::internalError("should not happen");
}

llvm::Function* J3CodeGen::buildFunction(J3Method* method, bool isStub) {
	const char* id = (isStub && !method->fnPtr()) ? method->llvmStubName(cl) : method->llvmFunctionName(cl);
	loader->addSymbol(id, method);
	return (llvm::Function*)module->getOrInsertFunction(id, method->signature()->functionType(method->access()));
}

llvm::Value* J3CodeGen::typeDescriptor(J3ObjectType* objectType, llvm::Type* type) {
	const char* id = objectType->nativeName();
	loader->addSymbol(id, objectType);
	llvm::Value* v = module->getOrInsertGlobal(id, vm->typeJ3ObjectType);
	return type == vm->typeJ3ObjectTypePtr ? v : builder.CreateBitCast(v, type);
}

llvm::Value* J3CodeGen::spToCurrentThread(llvm::Value* sp) {
	return builder.CreateIntToPtr(builder.CreateAnd(builder.CreatePtrToInt(sp, uintPtrTy),
																										llvm::ConstantInt::get(uintPtrTy, vmkit::Thread::getThreadMask())),
																 vm->typeJ3Thread);
}

llvm::Value* J3CodeGen::currentThread() {
	return spToCurrentThread(builder.CreateCall(frameAddress, builder.getInt32(0)));
}

void J3CodeGen::monitorEnter(llvm::Value* obj) {
	llvm::Type* recordTy = vm->typeJ3LockRecord;
	llvm::Type* recordPtrTy = vm->typeJ3LockRecord->getPointerTo();

	llvm::AllocaInst* recordPtr = builder.CreateAlloca(recordPtrTy);

	llvm::BasicBlock* ok = forwardBranch("lock-ok", codeReader->tell(), 0, 0);
	llvm::BasicBlock* stackLocked = newBB("stack-locked");
	llvm::BasicBlock* tryStackLock = newBB("try-stack-lock");
	llvm::BasicBlock* stackFail = newBB("stack-lock-fail");

	/* already stack locked by myself? */
	llvm::Value* gepH[] = { builder.getInt32(0), builder.getInt32(J3Object::gepHeader) };
	llvm::Value* headerPtr = builder.CreateGEP(obj, gepH);
	llvm::Value* header = builder.CreateLoad(headerPtr);
	
	builder.CreateStore(builder.CreateIntToPtr(header, recordPtrTy), recordPtr);
	builder.CreateCondBr(builder.CreateICmpEQ(currentThread(), spToCurrentThread(header)),
												stackLocked, tryStackLock);

	/* try to stack lock */
	builder.SetInsertPoint(tryStackLock);
	llvm::AllocaInst* record = builder.CreateAlloca(recordTy);
	builder.CreateStore(record, recordPtr);
	llvm::Value* gepR[] = { builder.getInt32(0), builder.getInt32(J3LockRecord::gepHeader) };
	builder.CreateStore(header, builder.CreateGEP(record, gepR));
	llvm::Value* gepC[] = { builder.getInt32(0), builder.getInt32(J3LockRecord::gepLockCount) };
	builder.CreateStore(builder.getInt32(0), builder.CreateGEP(record, gepC));
	llvm::Value* orig = builder.CreateOr(builder.CreateAnd(header, llvm::ConstantInt::get(uintPtrTy, ~6)), 
																				llvm::ConstantInt::get(uintPtrTy, 1)); /* ...001 */
	llvm::Value* res = builder.CreateAtomicCmpXchg(headerPtr, 
																									orig, 
																									builder.CreatePtrToInt(record, uintPtrTy),
																									llvm::SequentiallyConsistent, 
																									llvm::CrossThread);
	builder.CreateCondBr(builder.CreateICmpEQ(res, orig), stackLocked, stackFail);

	/* stack locked, increment the counter */
	builder.SetInsertPoint(stackLocked);
	llvm::Value* countPtr = builder.CreateGEP(builder.CreateLoad(recordPtr), gepC);
	builder.CreateStore(builder.CreateAdd(builder.CreateLoad(countPtr), builder.getInt32(1)), countPtr);
	builder.CreateBr(ok);

	/* unable to stack lock, fall back to monitor */
	builder.SetInsertPoint(stackFail);
	builder.CreateCall(funcJ3ObjectMonitorEnter, obj);
	builder.CreateBr(ok);
}

void J3CodeGen::monitorExit(llvm::Value* obj) {
	llvm::Type* recordPtrTy = vm->typeJ3LockRecord->getPointerTo();

	llvm::BasicBlock* ok = forwardBranch("unlock-ok", codeReader->tell(), 0, 0);
	llvm::BasicBlock* stackUnlock = newBB("stack-unlock");
	//llvm::BasicBlock* tryStackLock = newBB("try-stack-lock");
	llvm::BasicBlock* monitorUnlock = newBB("monitor-unlock");
	llvm::BasicBlock* stackRelease = newBB("stack-release");
	llvm::BasicBlock* stackRec = newBB("stack-rec");

	/* stack locked by myself? */
	llvm::Value* gepH[] = { builder.getInt32(0), builder.getInt32(J3Object::gepHeader) };
	llvm::Value* headerPtr = builder.CreateGEP(obj, gepH);
	llvm::Value* header = builder.CreateLoad(headerPtr);
	
	builder.CreateCondBr(builder.CreateICmpEQ(currentThread(), spToCurrentThread(header)),
												stackUnlock, monitorUnlock);
	
	/* ok, I'm the owner */
	builder.SetInsertPoint(stackUnlock);
	llvm::Value* gepC[] = { builder.getInt32(0), builder.getInt32(J3LockRecord::gepLockCount) };
	llvm::Value* recordPtr = builder.CreateIntToPtr(header, recordPtrTy);
	llvm::Value* countPtr = builder.CreateGEP(recordPtr, gepC);
	llvm::Value* count = builder.CreateSub(builder.CreateLoad(countPtr), builder.getInt32(1));
	builder.CreateCondBr(builder.CreateICmpEQ(count, builder.getInt32(0)), stackRelease, stackRec);

	/* last unlock */
	builder.SetInsertPoint(stackRelease);
	llvm::Value* gepR[] = { builder.getInt32(0), builder.getInt32(J3LockRecord::gepHeader) };
	llvm::Value* orig = builder.CreateLoad(builder.CreateGEP(recordPtr, gepR));
	llvm::Value* res = builder.CreateAtomicCmpXchg(headerPtr, 
																									header, 
																									orig,
																									llvm::SequentiallyConsistent, 
																									llvm::CrossThread);
	builder.CreateCondBr(builder.CreateICmpEQ(res, header), ok, monitorUnlock);

	/* recursive unlock */
	builder.SetInsertPoint(stackRec);
	builder.CreateStore(count, countPtr);
	builder.CreateBr(ok);

	/* monitor unlock */
	builder.SetInsertPoint(monitorUnlock);
	builder.CreateCall(funcJ3ObjectMonitorExit, obj);
	builder.CreateBr(ok);
}

void J3CodeGen::initialiseJ3ObjectType(J3ObjectType* cl) {
	if(!cl->isInitialised())
		builder.CreateCall(funcJ3TypeInitialise, typeDescriptor(cl, vm->typeJ3TypePtr));
}

llvm::Value* J3CodeGen::javaClass(J3ObjectType* type, bool doPush) {
	return builder.CreateCall3(funcJ3TypeJavaClass, 
															typeDescriptor(type, vm->typeJ3TypePtr), 
															builder.getInt1(doPush),
															builder.CreateIntToPtr(llvm::ConstantInt::get(uintPtrTy, (uintptr_t)0),
																											vm->typeJ3ObjectHandlePtr));
}

llvm::Value* J3CodeGen::handleToObject(llvm::Value* obj) {
	llvm::Value* gep[] = { builder.getInt32(0), builder.getInt32(J3ObjectHandle::gepObj) };
	return builder.CreateLoad(builder.CreateGEP(obj, gep));
}

llvm::Value* J3CodeGen::staticInstance(J3Class* cl) {
	initialiseJ3ObjectType(cl);
	return handleToObject(builder.CreateCall(funcJ3ClassStaticInstance, 
																						typeDescriptor(cl, vm->typeJ3ClassPtr)));
}

llvm::Value* J3CodeGen::vt(llvm::Value* obj) {
	llvm::Value* gepVT[] = { builder.getInt32(0),
													 builder.getInt32(J3Object::gepVT) };
	llvm::Instruction* res = builder.CreateLoad(builder.CreateGEP(obj, gepVT));
	res->setDebugLoc(llvm::DebugLoc::get(javaPC, 1, dbgInfo));
	return res;
}

llvm::Value* J3CodeGen::vt(J3ObjectType* type, bool doResolve) {
	llvm::Value* func = doResolve && !type->isResolved() ? funcJ3TypeVTAndResolve : funcJ3TypeVT;
	return builder.CreateCall(func, typeDescriptor(type, vm->typeJ3TypePtr));
}

llvm::Value* J3CodeGen::nullCheck(llvm::Value* obj) {
	if(exceptions.nodes[curExceptionNode]->landingPad) {
		llvm::BasicBlock* succeed = newBB("nullcheck-succeed");

		if(!bbNullCheckFailed) {
			llvm::BasicBlock* prev = builder.GetInsertBlock();
			bbNullCheckFailed = newBB("nullcheck-failed");
			builder.SetInsertPoint(bbNullCheckFailed);
			builder.CreateInvoke(funcNullPointerException, bbRet, exceptions.nodes[curExceptionNode]->landingPad);
			builder.SetInsertPoint(prev);
		}

		builder.CreateCondBr(builder.CreateIsNotNull(obj), succeed, bbNullCheckFailed);
		builder.SetInsertPoint(succeed);
	}

	return obj;
}

#define nyi() J3::internalError("not yet implemented: '%s' (%d)", J3Cst::opcodeNames[bc], bc);

void J3CodeGen::invoke(uint32_t access, J3Method* target, llvm::Value* func) {
	J3Signature* type = target->signature();
	llvm::FunctionType* fType = target->signature()->functionType(access);
	uint32_t n = fType->getNumParams();
	std::vector<llvm::Value*> args;
	uint32_t i=n-1;

	for(llvm::FunctionType::param_iterator it=fType->param_begin(); it!=fType->param_end(); it++)
		args.push_back(unflatten(stack.top(i--), *it));

	stack.drop(n);

	llvm::Value* res;

	if(exceptions.nodes[curExceptionNode]->landingPad) {
		//llvm::BasicBlock* after = forwardBranch("invoke-after", codeReader->tell(), 0, 0);
		llvm::BasicBlock* after = newBB("invoke-after");
		res = builder.CreateInvoke(func, after, exceptions.nodes[curExceptionNode]->landingPad, args);
		builder.SetInsertPoint(after);
	} else {
		res = builder.CreateCall(func, args);
	}
	
	if(!res->getType()->isVoidTy())
		stack.push(flatten(res));
}

void J3CodeGen::invokeInterface(uint32_t idx) {
	J3Method* target = cl->interfaceMethodAt(idx, 0);
	J3Signature* type = target->signature();

	uint32_t     index = target->interfaceIndex();
	llvm::Value* thread = currentThread();
	llvm::Value* gep[] = { builder.getInt32(0), builder.getInt32(J3Thread::gepInterfaceMethodIndex) };
	builder.CreateStore(builder.getInt32(index), builder.CreateGEP(thread, gep));

	llvm::Value*  obj = nullCheck(stack.top(type->nbIns()));
	llvm::Value*  gepFunc[] = { builder.getInt32(0),
															builder.getInt32(J3VirtualTable::gepInterfaceMethods),
															builder.getInt32(index % J3VirtualTable::nbInterfaceMethodTable) };
	llvm::Value* func = builder.CreateBitCast(builder.CreateLoad(builder.CreateGEP(vt(obj), gepFunc)), 
																						 target->signature()->functionType(target->access())->getPointerTo());

	invoke(0, target, func);
}

void J3CodeGen::invokeVirtual(uint32_t idx) {
	J3Method*     target = cl->methodAt(idx, 0);
	J3Signature* type = target->signature();
	llvm::Value*  funcEntry = funcEntry = builder.getInt32(target->index());

	llvm::Value*  obj = nullCheck(stack.top(type->nbIns()));
	llvm::Value*  gepFunc[] = { builder.getInt32(0),
															builder.getInt32(J3VirtualTable::gepVirtualMethods),
															funcEntry };
	llvm::Value* func = builder.CreateBitCast(builder.CreateLoad(builder.CreateGEP(vt(obj), gepFunc)), 
																						 target->signature()->functionType(target->access())->getPointerTo());

	invoke(0, target, func);
}

void J3CodeGen::invokeStatic(uint32_t idx) {
	J3Method* target = cl->methodAt(idx, J3Cst::ACC_STATIC);
	invoke(J3Cst::ACC_STATIC, target, buildFunction(target));
}

void J3CodeGen::invokeSpecial(uint32_t idx) {
	J3Method* target = cl->methodAt(idx, 0);
	invoke(0, target, buildFunction(target));
}

llvm::Value* J3CodeGen::fieldOffset(llvm::Value* obj, J3Field* f) {
	return builder.CreateIntToPtr(builder.CreateAdd(builder.CreatePtrToInt(obj, uintPtrTy),
																										llvm::ConstantInt::get(uintPtrTy, f->offset())),
																 f->type()->llvmType()->getPointerTo());
}

void J3CodeGen::get(llvm::Value* src, J3Field* f) {
	llvm::Value* res = flatten(builder.CreateLoad(fieldOffset(src, f)));
	stack.push(res);
}

void J3CodeGen::getField(uint32_t idx) {
	llvm::Value* obj = stack.pop(); 
	J3Field* f = cl->fieldAt(idx, 0);
	get(nullCheck(obj), f);
}

void J3CodeGen::getStatic(uint32_t idx) {
	J3Field* f = cl->fieldAt(idx, J3Cst::ACC_STATIC);
	get(staticInstance(f->layout()->asStaticLayout()->cl()), f);
}

void J3CodeGen::put(llvm::Value* dest, llvm::Value* val, J3Field* f) {
	builder.CreateStore(unflatten(val, f->type()->llvmType()), fieldOffset(dest, f));
}

void J3CodeGen::putStatic(uint32_t idx) {
	J3Field* f = cl->fieldAt(idx, J3Cst::ACC_STATIC);
	put(staticInstance(f->layout()->asStaticLayout()->cl()), stack.pop(), f);
}

void J3CodeGen::putField(uint32_t idx) {
	J3Field* f = cl->fieldAt(idx, 0);
	llvm::Value* val = stack.pop();
	llvm::Value* obj = nullCheck(stack.pop());
	put(obj, val, f);
}

void J3CodeGen::arrayBoundCheck(llvm::Value* obj, llvm::Value* idx) {
}

llvm::Value* J3CodeGen::arrayContent(J3Type* cType, llvm::Value* array, llvm::Value* idx) {
	array = builder.CreateBitCast(array, vm->typeJ3ArrayObjectPtr);
	return builder.CreateGEP(builder.CreateBitCast(builder.CreateGEP(array, builder.getInt32(1)), cType->llvmType()->getPointerTo()),
														idx);
}

void J3CodeGen::arrayStore(J3Type* cType) {
	llvm::Value* val = stack.pop();
	llvm::Value* idx = stack.pop();
	llvm::Value* array = stack.pop();

	arrayBoundCheck(array, idx);
	builder.CreateStore(unflatten(val, cType->llvmType()), arrayContent(cType, array, idx));
}

void J3CodeGen::arrayLoad(J3Type* cType) {
	llvm::Value* idx = stack.pop();
	llvm::Value* array = stack.pop();

	arrayBoundCheck(array, idx);
	stack.push(flatten(builder.CreateLoad(arrayContent(cType, array, idx))));
}

llvm::Value* J3CodeGen::arrayLengthPtr(llvm::Value* obj) {
	llvm::Value* gep[2] = { builder.getInt32(0), builder.getInt32(J3ArrayObject::gepLength) };
	return builder.CreateGEP(builder.CreateBitCast(obj, vm->typeJ3ArrayObjectPtr), gep);
}

llvm::Value* J3CodeGen::arrayLength(llvm::Value* obj) {
	return builder.CreateLoad(arrayLengthPtr(obj));
}

void J3CodeGen::newArray(J3ArrayClass* array) {
	initialiseJ3ObjectType(array);
	llvm::Value* length = stack.pop();
	llvm::Value* nbb = 
		builder.CreateAdd(llvm::ConstantInt::get(uintPtrTy, sizeof(J3ArrayObject)),
											 builder.CreateMul(llvm::ConstantInt::get(uintPtrTy, 1 << array->component()->logSize()),
																					builder.CreateZExtOrBitCast(length, uintPtrTy)));
	
	llvm::Value* res = builder.CreateCall2(funcJ3ObjectAllocate, vt(array), nbb);

	builder.CreateStore(length, arrayLengthPtr(res));

	stack.push(res);
}

void J3CodeGen::newArray(uint8_t atype) {
	J3Primitive* prim = 0;

	switch(atype) {
		case J3Cst::T_BOOLEAN: prim = vm->typeBoolean; break;
		case J3Cst::T_CHAR:    prim = vm->typeCharacter; break;
		case J3Cst::T_FLOAT:   prim = vm->typeFloat; break;
		case J3Cst::T_DOUBLE:  prim = vm->typeDouble; break;
		case J3Cst::T_BYTE:    prim = vm->typeByte; break;
		case J3Cst::T_SHORT:   prim = vm->typeShort; break;
		case J3Cst::T_INT:     prim = vm->typeInteger; break;
		case J3Cst::T_LONG:    prim = vm->typeLong; break;
		default:
			J3::classFormatError(cl, "wrong atype: %d\n", atype);
	}

	newArray(prim->getArray());
}

void J3CodeGen::multianewArray() {
	J3ObjectType* base = cl->classAt(codeReader->readU2());
	uint32_t dim = codeReader->readU1();

	llvm::Value* values = builder.CreateAlloca(builder.getInt32Ty(), builder.getInt32(dim));
	
	for(uint32_t i=0; i<dim; i++)
		builder.CreateStore(stack.pop(), builder.CreateGEP(values, builder.getInt32(dim-i-1)));

	stack.push(builder.CreateCall3(funcJ3ArrayObjectMultianewArray, 
																	typeDescriptor(base, vm->typeJ3ArrayClassPtr), 
																	builder.getInt32(dim), 
																	values));
}

void J3CodeGen::newObject(J3Class* cl) {
	initialiseJ3ObjectType(cl);

	llvm::Value* size;

	if(!cl->isResolved()) {
		size = builder.CreateCall(funcJ3LayoutStructSize, typeDescriptor(cl, vm->typeJ3LayoutPtr));
	} else {
		size = builder.getInt64(cl->structSize());
	}

	llvm::Value* res = builder.CreateCall2(funcJ3ObjectAllocate, vt(cl), size);

	stack.push(res);
}

llvm::Value* J3CodeGen::isAssignableTo(llvm::Value* obj, J3ObjectType* type) {
	llvm::Value* vtType = vt(type, 1);
	llvm::Value* vtObj = vt(obj);

	if(type->isResolved()) {
		if(type->vt()->isPrimaryChecker())
			return builder.CreateCall3(funcFastIsAssignableToPrimaryChecker, 
																	vtObj, 
																	vtType,
																	builder.getInt32(type->vt()->offset()));
		else 
			return builder.CreateCall2(funcFastIsAssignableToNonPrimaryChecker, vtObj, vtType);
	} else {
		return builder.CreateCall2(funcIsAssignableTo, vtObj, vtType);
	}
}

void J3CodeGen::instanceof(llvm::Value* obj, J3ObjectType* type) {
	llvm::BasicBlock* after = forwardBranch("instanceof-after", codeReader->tell(), 0, 0);
	llvm::BasicBlock* nok = newBB("instanceof-null");
	llvm::BasicBlock* test = newBB("instanceof");

	builder.CreateCondBr(builder.CreateIsNull(obj), nok, test);

	builder.SetInsertPoint(nok);
	stack.push(builder.getInt32(0));
	builder.CreateBr(after);

	stack.drop(1);
	builder.SetInsertPoint(test);
	stack.push(builder.CreateZExt(isAssignableTo(obj, type), builder.getInt32Ty()));
	builder.CreateBr(after);
}

void J3CodeGen::checkCast(llvm::Value* obj, J3ObjectType* type) {
	llvm::BasicBlock* succeed = forwardBranch("checkcast-succeed", codeReader->tell(), 0, 0);
	llvm::BasicBlock* test = newBB("checkcast");

	builder.CreateCondBr(builder.CreateIsNull(obj), succeed, test);

	if(!bbCheckCastFailed) {
		bbCheckCastFailed = newBB("checkcast-failed");
		builder.SetInsertPoint(bbCheckCastFailed);
		builder.CreateCall(funcClassCastException);
		builder.CreateBr(bbRet);
	}

	builder.SetInsertPoint(test);

	llvm::Value* res = isAssignableTo(obj, type);

	builder.CreateCondBr(res, succeed, bbCheckCastFailed);
}

void J3CodeGen::floatToInteger(J3Type* ftype, J3Type* itype) {
	llvm::Value* min = llvm::ConstantFP::get(ftype->llvmType(), 
																					 llvm::APInt::getSignedMinValue(itype->llvmType()->getPrimitiveSizeInBits()).getSExtValue());
	llvm::Value* max = llvm::ConstantFP::get(ftype->llvmType(), 
																					 llvm::APInt::getSignedMaxValue(itype->llvmType()->getPrimitiveSizeInBits()).getZExtValue());
	llvm::Value* v = stack.pop();

	llvm::Value* c = builder.CreateFCmpONE(v, v);
	v = builder.CreateSelect(c, llvm::ConstantFP::get(ftype->llvmType(), 0), v); /* nan => 0 */

	c = builder.CreateFCmpOGE(v, max);
	v = builder.CreateSelect(c, max, v);
	c = builder.CreateFCmpOLE(v, min);
	v = builder.CreateSelect(c, min, v);

	stack.push(builder.CreateFPToSI(v, itype->llvmType()));
}

void J3CodeGen::compareLong() {
	llvm::Value* val2 = stack.pop();
	llvm::Value* val1 = stack.pop();
	llvm::Value* one = builder.getInt32(1);
	llvm::Value* zero = builder.getInt32(0);
	llvm::Value* minus = builder.getInt32(-1);

	llvm::Value* c = builder.CreateICmpSGT(val1, val2);
	llvm::Value* r = builder.CreateSelect(c, one, zero);
	c = builder.CreateICmpSLT(val1, val2);
	r = builder.CreateSelect(c, minus, r);

	stack.push(r);
}

void J3CodeGen::compareFP(bool isL) {
	llvm::Value* val2 = stack.pop();
	llvm::Value* val1 = stack.pop();
	llvm::Value* one = builder.getInt32(1);
	llvm::Value* zero = builder.getInt32(0);
	llvm::Value* minus = builder.getInt32(-1);

	llvm::Value* c = builder.CreateFCmpUGT(val1, val2);
	llvm::Value* r = builder.CreateSelect(c, one, zero);
  c = builder.CreateFCmpULT(val1, val2);
  r = builder.CreateSelect(c, minus, r);
  c = builder.CreateFCmpUNO(val1, val2);
  r = builder.CreateSelect(c, isL ? one : minus, r);

  stack.push(r);
}

void J3CodeGen::ldc(uint32_t idx) {
	llvm::Value* res;

	switch(cl->getCtpType(idx)) {
		case J3Cst::CONSTANT_Long:    res = builder.getInt64(cl->longAt(idx)); break;
		case J3Cst::CONSTANT_Integer: res = builder.getInt32(cl->integerAt(idx)); break;
		case J3Cst::CONSTANT_Float:   res = llvm::ConstantFP::get(builder.getFloatTy(), cl->floatAt(idx)); break;
		case J3Cst::CONSTANT_Double:  res = llvm::ConstantFP::get(builder.getDoubleTy(), cl->doubleAt(idx)); break;
		case J3Cst::CONSTANT_Class:   res = handleToObject(javaClass(cl->classAt(idx), 0)); break;
		case J3Cst::CONSTANT_String:  
			res = handleToObject(builder.CreateCall3(funcJ3ClassStringAt, 
																								typeDescriptor(cl, vm->typeJ3ClassPtr),
																								builder.getInt16(idx),
																								builder.getInt1(0)));
			break;
		default:
			J3::classFormatError(cl, "wrong ldc type: %d\n", cl->getCtpType(idx));
	}
	stack.push(res);
}

void J3CodeGen::lookupSwitch() {
	codeReader->seek(((codeReader->tell() - 1) & -4) + 4, J3Reader::SeekSet);
	llvm::Value* val = stack.pop();
	llvm::BasicBlock* def = forwardBranch("lookupswitch-default", javaPC + codeReader->readU4(), 1, 1);
	uint32_t n = codeReader->readU4();
	
	for(uint32_t i=0; i<n; i++) {
		int32_t match = codeReader->readS4();
		llvm::BasicBlock* ok = forwardBranch("lookupswitch-match", javaPC + codeReader->readS4(), 1, 1);
		llvm::BasicBlock* nok = i == (n - 1) ? def : newBB("lookupswitch-next");
		builder.CreateCondBr(builder.CreateICmpEQ(val, builder.getInt32(match)), ok, nok);
		builder.SetInsertPoint(nok);
	}
}

void J3CodeGen::tableSwitch() {
	codeReader->seek(((codeReader->tell() - 1) & -4) + 4, J3Reader::SeekSet);
	llvm::Value* val = stack.pop();
	llvm::BasicBlock* def = forwardBranch("tableswitch-default", javaPC + codeReader->readU4(), 1, 1);
	int32_t low = codeReader->readU4();
	int32_t high = codeReader->readU4();
	llvm::SwitchInst* dispatch = builder.CreateSwitch(val, def, high - low + 1);

	for(uint32_t i=low; i<=high; i++)
		dispatch->addCase(builder.getInt32(i),
											forwardBranch("tableswitch-match", javaPC + codeReader->readU4(), 1, 1));
}

llvm::BasicBlock* J3CodeGen::newBB(const char* name) {
	return llvm::BasicBlock::Create(llvmFunction->getContext(), name, llvmFunction);
}

void J3CodeGen::condBr(llvm::Value* op) {
	builder.CreateCondBr(op,
												forwardBranch("if-true", javaPC + codeReader->readS2(), 1, 1),
												forwardBranch("if-false", codeReader->tell(), 0, 0));
}

llvm::BasicBlock* J3CodeGen::forwardBranch(const char* id, uint32_t pc, bool doAlloc, bool doPush) {
	llvm::BasicBlock* res = opInfos[pc].bb;

	if(res) 
		return res;

	if(vm->options()->debugTranslate > 2)
		fprintf(stderr, "        forward branch at %d\n", pc);

	if(opInfos[pc].insn) {
		//printf("split at %d (%s)\n", pc, id);
		llvm::Instruction* insn = opInfos[pc].insn;
		if(!insn)
			J3::classFormatError(cl, "jmp: not to an instruction");
		insn = insn->getNextNode();
		//fprintf(stderr, "--- instruction ---\n");
		//insn->dump();
		llvm::BasicBlock* before = insn->getParent();
		llvm::BranchInst* fakeTerminator = 0;
		bool isSelf = builder.GetInsertBlock() == before;

		//fprintf(stderr, "--- before split ---\n");
		//before->dump();
		if(!before->getTerminator())
			fakeTerminator = llvm::BranchInst::Create(bbRet, before);

		llvm::BasicBlock* after = before->splitBasicBlock(insn);

		if(fakeTerminator)
			fakeTerminator->eraseFromParent();

		if(isSelf)
			builder.SetInsertPoint(after);

		//fprintf(stderr, "--- after split ---\n");
		//before->dump();
		//after->dump();

		opInfos[pc].bb = after;
		return after;
	} else {
		llvm::BasicBlock* res = newBB(id);

		if(doAlloc) {
			opInfos[pc].metaStack = (llvm::Type**)allocator->allocate(sizeof(llvm::Type*)*stack.maxStack);
			memcpy(opInfos[pc].metaStack, stack.metaStack, sizeof(llvm::Type*)*stack.topStack);
		}

		opInfos[pc].bb = res;
		opInfos[pc].topStack = stack.topStack;
		if(doPush)
			pendingBranchs[topPendingBranchs++] = pc;
			
		return res;
	}
}

bool J3CodeGen::onEndPoint() {
	uint32_t pc;
	do {
		if(!topPendingBranchs)
			return 1;
		pc = pendingBranchs[--topPendingBranchs];
	} while(opInfos[pc].insn);
	closeBB = 0;
	codeReader->seek(pc, codeReader->SeekSet);
	return 0;
}

void J3CodeGen::selectExceptionNode(uint32_t idx) {
	curExceptionNode = idx;

	if(!exceptions.nodes[idx]->isAdded) {
		exceptions.nodes[idx]->isAdded = 1;
		for(uint32_t i=0; i<exceptions.nodes[idx]->nbEntries; i++) {
			J3ExceptionEntry* e = exceptions.nodes[idx]->entries[i];
			if(!e->isAdded) {
				e->isAdded = 1;
				pendingBranchs[topPendingBranchs++] = e->handlerPC;
			}
		}
	}
}

llvm::Value* J3CodeGen::buildString(const char* msg) {
	std::vector<llvm::Constant*> elmts;
	uint32_t n;

	for(n=0; msg[n]; n++)
		elmts.push_back(builder.getInt8(msg[n]));

	elmts.push_back(builder.getInt8(0));

	llvm::Constant* str = llvm::ConstantArray::get(llvm::ArrayType::get(builder.getInt8Ty(), n+1), elmts);
	llvm::Value* var = new llvm::GlobalVariable(*module,
																							str->getType(),
																							1,
																							llvm::GlobalVariable::InternalLinkage,
																							str);
	llvm::Value* gep[] = { builder.getInt32(0), builder.getInt32(0) };

	return builder.CreateGEP(var, gep);
}

void J3CodeGen::translate() {
	if(vm->options()->debugTranslate > 1)
		exceptions.dump(vm->options()->debugTranslate-1);

	stack.topStack = 0;
	_onEndPoint();
	closeBB = 1;

	selectExceptionNode(0);

	while(codeReader->remaining()) {
		llvm::Value* val1;
		llvm::Value* val2;
		llvm::Value* val3;

		javaPC = codeReader->tell();

		if(javaPC < exceptions.nodes[curExceptionNode]->pc || javaPC >= exceptions.nodes[curExceptionNode+1]->pc) {
			if(javaPC == exceptions.nodes[curExceptionNode+1]->pc)
				selectExceptionNode(curExceptionNode+1);
			else
				for(uint32_t i=0; i<exceptions.nbNodes; i++)
					if(exceptions.nodes[i]->pc <= javaPC && javaPC < exceptions.nodes[i+1]->pc) {
						selectExceptionNode(i);
						break;
					}
			//printf("cur exception node: %d\n", curExceptionNode);
		}

		if(opInfos[javaPC].insn || opInfos[javaPC].bb) {
			if(closeBB && !builder.GetInsertBlock()->getTerminator()) {
				if(!opInfos[javaPC].bb)
					J3::internalError("random split???");
				builder.CreateBr(opInfos[javaPC].bb);
			}
		}

		if(opInfos[javaPC].insn) {
			_onEndPoint();
			javaPC = codeReader->tell();
		}

		closeBB = 1;

		if(opInfos[javaPC].bb) {
			builder.SetInsertPoint(opInfos[javaPC].bb);
			//printf("Meta stack before: %p\n", metaStack);
			if(opInfos[javaPC].metaStack) {
				stack.metaStack = opInfos[javaPC].metaStack;
				stack.topStack = opInfos[javaPC].topStack;
			} else if(opInfos[javaPC].topStack == -1) { /* exception handling */
				stack.metaStack[0] = vm->typeJ3ObjectPtr;
				stack.topStack = 1;
			}
			//printf("Meta stack after: %p\n", metaStack);
		}
		
		if(opInfos[javaPC].bb || builder.GetInsertBlock()->empty())
			opInfos[javaPC].insn = builder.GetInsertBlock()->begin();
		else 
			opInfos[javaPC].insn = builder.GetInsertBlock()->end()->getPrevNode();

		bc = codeReader->readU1();

		switch(vm->options()->debugTranslate) {
			default:
			case 5:
				fprintf(stderr, "--------------------------------------------\n");
				llvmFunction->dump();
			case 4:
				stack.dump();
			case 3:
			case 2:
				fprintf(stderr, "    [%4d] decoding: %s\n", javaPC, J3Cst::opcodeNames[bc]);
				break;
			case 1:
			case 0:
				break;
		}

		genDebugOpcode();

		switch(bc) {
			case J3Cst::BC_nop:                           /* 0x00 */
				break;

			case J3Cst::BC_aconst_null:                   /* 0x01 */
				stack.push(nullValue);
				break;

			case J3Cst::BC_iconst_m1:                     /* 0x02 */
			case J3Cst::BC_iconst_0:                      /* 0x03 */
			case J3Cst::BC_iconst_1:                      /* 0x04 */
			case J3Cst::BC_iconst_2:                      /* 0x05 */
			case J3Cst::BC_iconst_3:                      /* 0x06 */
			case J3Cst::BC_iconst_4:                      /* 0x07 */
			case J3Cst::BC_iconst_5:                      /* 0x08 */
				stack.push(builder.getInt32(bc - J3Cst::BC_iconst_0));
				break;

			case J3Cst::BC_lconst_0:                      /* 0x09 */
			case J3Cst::BC_lconst_1:                      /* 0x0a */
				stack.push(builder.getInt64(bc - J3Cst::BC_lconst_0));
				break;

			case J3Cst::BC_fconst_0:                      /* 0x0b */
			case J3Cst::BC_fconst_1:                      /* 0x0c */
			case J3Cst::BC_fconst_2:                      /* 0x0d */
				stack.push(llvm::ConstantFP::get(builder.getFloatTy(), (bc - J3Cst::BC_fconst_0)));
				break;

			case J3Cst::BC_dconst_0:                      /* 0x0e */
			case J3Cst::BC_dconst_1:                      /* 0x0f */
				stack.push(llvm::ConstantFP::get(builder.getDoubleTy(), (bc - J3Cst::BC_dconst_0)));
				break;

			case J3Cst::BC_bipush:                        /* 0x10 */
				stack.push(builder.getInt32(codeReader->readS1()));
				break;

			case J3Cst::BC_sipush:                        /* 0x11 */
				stack.push(builder.getInt32(codeReader->readS2()));
				break;

			case J3Cst::BC_ldc:                           /* 0x12 */
				ldc(codeReader->readU1());
				break;

			case J3Cst::BC_ldc_w:                         /* 0x13 */
			case J3Cst::BC_ldc2_w:                        /* 0x14 */
				ldc(codeReader->readU2());
				break;

			case J3Cst::BC_iload:                         /* 0x15 wide */
				stack.push(locals.at(wideReadU1(), vm->typeInteger->llvmType()));
				break;

			case J3Cst::BC_lload:                         /* 0x16 wide */
				stack.push(locals.at(wideReadU1(), vm->typeLong->llvmType()));
				break;

			case J3Cst::BC_fload:                         /* 0x17 wide */
				stack.push(locals.at(wideReadU1(), vm->typeFloat->llvmType()));
				break;

			case J3Cst::BC_dload:                         /* 0x18 wide */
				stack.push(locals.at(wideReadU1(), vm->typeDouble->llvmType()));
				break;

			case J3Cst::BC_aload:                         /* 0x19 wide */				
				stack.push(locals.at(wideReadU1(), vm->objectClass->llvmType()));
				break;

			case J3Cst::BC_iload_0:                       /* 0x1a */
			case J3Cst::BC_iload_1:                       /* 0x1b */
			case J3Cst::BC_iload_2:                       /* 0x1c */
			case J3Cst::BC_iload_3:                       /* 0x1d */
				stack.push(locals.at(bc - J3Cst::BC_iload_0, vm->typeInteger->llvmType()));
				break;

			case J3Cst::BC_lload_0:                       /* 0x1e */
			case J3Cst::BC_lload_1:                       /* 0x1f */
			case J3Cst::BC_lload_2:                       /* 0x20 */
			case J3Cst::BC_lload_3:                       /* 0x21 */
				stack.push(locals.at(bc - J3Cst::BC_lload_0, vm->typeLong->llvmType()));
				break;

			case J3Cst::BC_fload_0:                       /* 0x22 */
			case J3Cst::BC_fload_1:                       /* 0x23 */
			case J3Cst::BC_fload_2:                       /* 0x24 */
			case J3Cst::BC_fload_3:                       /* 0x25 */
				stack.push(locals.at(bc - J3Cst::BC_fload_0, vm->typeFloat->llvmType()));
				break;

			case J3Cst::BC_dload_0:                       /* 0x26 */
			case J3Cst::BC_dload_1:                       /* 0x27 */
			case J3Cst::BC_dload_2:                       /* 0x28 */
			case J3Cst::BC_dload_3:                       /* 0x29 */
				stack.push(locals.at(bc - J3Cst::BC_dload_0, vm->typeDouble->llvmType()));
				break;

			case J3Cst::BC_aload_0:                       /* 0x2a */
			case J3Cst::BC_aload_1:                       /* 0x2b */
			case J3Cst::BC_aload_2:                       /* 0x2c */
			case J3Cst::BC_aload_3:                       /* 0x2d */
				stack.push(locals.at(bc - J3Cst::BC_aload_0, vm->objectClass->llvmType()));
				break;

			case J3Cst::BC_iaload:                        /* 0x2e */
				arrayLoad(vm->typeInteger);
				break;

			case J3Cst::BC_laload:                        /* 0x2f */
				arrayLoad(vm->typeLong);
				break;

			case J3Cst::BC_faload:                        /* 0x30 */
				arrayLoad(vm->typeFloat);
				break;

			case J3Cst::BC_daload:                        /* 0x31 */
				arrayLoad(vm->typeDouble);
				break;

			case J3Cst::BC_aaload:                        /* 0x32 */
				arrayLoad(vm->objectClass);
				break;

			case J3Cst::BC_baload:                        /* 0x33 */
				arrayLoad(vm->typeByte);
				break;

			case J3Cst::BC_caload:                        /* 0x34 */
				arrayLoad(vm->typeCharacter);
				break;

			case J3Cst::BC_saload:                        /* 0x35 */
				arrayLoad(vm->typeShort);
				break;

			case J3Cst::BC_istore:                        /* 0x36 wide */
			case J3Cst::BC_lstore:                        /* 0x37 wide */
			case J3Cst::BC_fstore:                        /* 0x38 wide */
			case J3Cst::BC_dstore:                        /* 0x39 wide */
			case J3Cst::BC_astore:                        /* 0x3a wide */
				locals.setAt(stack.pop(), wideReadU1());
				break;

			case J3Cst::BC_istore_0:                      /* 0x3b */
			case J3Cst::BC_istore_1:                      /* 0x3c */
			case J3Cst::BC_istore_2:                      /* 0x3d */
			case J3Cst::BC_istore_3:                      /* 0x3e */
				locals.setAt(stack.pop(), bc - J3Cst::BC_istore_0);
				break;

			case J3Cst::BC_lstore_0:                      /* 0x3f */
			case J3Cst::BC_lstore_1:                      /* 0x40 */
			case J3Cst::BC_lstore_2:                      /* 0x41 */
			case J3Cst::BC_lstore_3:                      /* 0x42 */
				locals.setAt(stack.pop(), bc - J3Cst::BC_lstore_0);
				break;

			case J3Cst::BC_fstore_0:                      /* 0x43 */
			case J3Cst::BC_fstore_1:                      /* 0x44 */
			case J3Cst::BC_fstore_2:                      /* 0x45 */
			case J3Cst::BC_fstore_3:                      /* 0x46 */
				locals.setAt(stack.pop(), bc - J3Cst::BC_fstore_0);
				break;

			case J3Cst::BC_dstore_0:                      /* 0x47 */
			case J3Cst::BC_dstore_1:                      /* 0x48 */
			case J3Cst::BC_dstore_2:                      /* 0x49 */
			case J3Cst::BC_dstore_3:                      /* 0x4a */
				locals.setAt(stack.pop(), bc - J3Cst::BC_dstore_0);
				break;

			case J3Cst::BC_astore_0:                      /* 0x4b */
			case J3Cst::BC_astore_1:                      /* 0x4c */
			case J3Cst::BC_astore_2:                      /* 0x4d */
			case J3Cst::BC_astore_3:                      /* 0x4e */
				locals.setAt(stack.pop(), bc - J3Cst::BC_astore_0);
				break;

			case J3Cst::BC_iastore:                       /* 0x4f */
				arrayStore(vm->typeInteger);
				break; 

			case J3Cst::BC_lastore:                       /* 0x50 */
				arrayStore(vm->typeLong);
				break;

			case J3Cst::BC_fastore:                       /* 0x51 */
				arrayStore(vm->typeFloat);
				break;

			case J3Cst::BC_dastore:                       /* 0x52 */
				arrayStore(vm->typeDouble);
				break;

			case J3Cst::BC_aastore:                       /* 0x53 */
				arrayStore(vm->objectClass);
				break;
				
			case J3Cst::BC_bastore:                       /* 0x54 */
				arrayStore(vm->typeByte);
				break;

			case J3Cst::BC_castore:                       /* 0x55 */
				arrayStore(vm->typeCharacter);
				break;

			case J3Cst::BC_sastore:                       /* 0x56 */
				arrayStore(vm->typeShort);
				break;

			case J3Cst::BC_pop:                           /* 0x57 */
				stack.pop();
				break;

			case J3Cst::BC_pop2:                          /* 0x58 */
				val1 = stack.pop();
				if(!val1->getType()->isDoubleTy() && !val1->getType()->isIntegerTy(64))
					stack.pop();
				break;

			case J3Cst::BC_dup:                           /* 0x59 */
				stack.push(stack.top());
				break;

			case J3Cst::BC_dup_x1:                        /* 0x5a */
				val1 = stack.pop(); val2 = stack.pop();
				stack.push(val1); stack.push(val2); stack.push(val1);
				break;

			case J3Cst::BC_dup_x2:                        /* 0x5b */
				val1 = stack.pop();
				val2 = stack.pop();
				if(val2->getType()->isDoubleTy() || val2->getType()->isIntegerTy(64)) {
					stack.push(val1); stack.push(val2); stack.push(val1);
				} else {
					val3 = stack.pop();
					stack.push(val1); stack.push(val3); stack.push(val2); stack.push(val1);
				}
				break;

			case J3Cst::BC_dup2:                          /* 0x5c */
				val1 = stack.top();
				if(val1->getType()->isDoubleTy() || val1->getType()->isIntegerTy(64)) {
					stack.push(val1);
				} else {
					val2 = stack.top(1);
					stack.push(val2); stack.push(val1);
				}
				break;

			case J3Cst::BC_dup2_x1:                       /* 0x5d */
				val1 = stack.pop();
				val2 = stack.pop();
				if(val1->getType()->isDoubleTy() || val1->getType()->isIntegerTy(64)) {
					stack.push(val1); stack.push(val2); stack.push(val1);
				} else {
					val3 = stack.pop();
					stack.push(val2); stack.push(val1); stack.push(val3); stack.push(val2); stack.push(val1);
				}
				break;

			case J3Cst::BC_dup2_x2:                       /* 0x5e */
				val1 = stack.pop();
				val2 = stack.pop();
				val3 = stack.pop();
				if(val1->getType()->isDoubleTy() || val1->getType()->isIntegerTy(64)) {
					stack.push(val1); stack.push(val3); stack.push(val2); stack.push(val1);
				} else {
					llvm::Value* val4 = stack.pop();
					stack.push(val2); stack.push(val1); stack.push(val4); stack.push(val3); stack.push(val2); stack.push(val1);
				}
				break;

			case J3Cst::BC_swap:                          /* 0x5f */
				val1 = stack.pop(); val2 = stack.pop(); stack.push(val1); stack.push(val2);
				break;

			case J3Cst::BC_iadd:                          /* 0x60 */
			case J3Cst::BC_ladd:                          /* 0x61 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateAdd(val1, val2));
				break;

			case J3Cst::BC_fadd:                          /* 0x62 */
			case J3Cst::BC_dadd:                          /* 0x63 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateFAdd(val1, val2));
				break;

			case J3Cst::BC_isub:                          /* 0x64 */
			case J3Cst::BC_lsub:                          /* 0x65 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateSub(val1, val2));
				break;

			case J3Cst::BC_fsub:                          /* 0x66 */
			case J3Cst::BC_dsub:                          /* 0x67 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateFSub(val1, val2));
				break;

			case J3Cst::BC_imul:                          /* 0x68 */
			case J3Cst::BC_lmul:                          /* 0x69 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateMul(val1, val2));
				break;

			case J3Cst::BC_fmul:                          /* 0x6a */
			case J3Cst::BC_dmul:                          /* 0x6b */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateFMul(val1, val2)); 
				break;

			case J3Cst::BC_idiv:                          /* 0x6c */
			case J3Cst::BC_ldiv:                          /* 0x6d */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateSDiv(val1, val2));
				break;

			case J3Cst::BC_fdiv:                          /* 0x6e */
			case J3Cst::BC_ddiv:                          /* 0x6f */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateFDiv(val1, val2));
				break;

			case J3Cst::BC_irem:                          /* 0x70 */
			case J3Cst::BC_lrem:                          /* 0x71 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateSRem(val1, val2));
				break;

			case J3Cst::BC_frem:                          /* 0x72 */
			case J3Cst::BC_drem:                          /* 0x73 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateFRem(val1, val2));
				break;

			case J3Cst::BC_ineg:                          /* 0x74 */
			case J3Cst::BC_lneg:                          /* 0x75 */
				stack.push(builder.CreateNeg(stack.pop()));
				break;

			case J3Cst::BC_fneg:                          /* 0x76 */
			case J3Cst::BC_dneg:                          /* 0x77 */
				stack.push(builder.CreateFNeg(stack.pop()));
				break;

			case J3Cst::BC_ishl:                          /* 0x78 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateShl(val1, builder.CreateAnd(val2, 0x1f)));
				break;

			case J3Cst::BC_lshl:                          /* 0x79 */
				val2 = stack.pop(); val1 = stack.pop(); 
				stack.push(builder.CreateShl(val1, builder.CreateZExt(builder.CreateAnd(val2, 0x3f), builder.getInt64Ty())));
				break;

			case J3Cst::BC_ishr:                          /* 0x7a */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateAShr(val1, builder.CreateAnd(val2, 0x1f)));
				break;

			case J3Cst::BC_lshr:                          /* 0x7b */
				val2 = stack.pop(); val1 = stack.pop(); 
				stack.push(builder.CreateAShr(val1, builder.CreateZExt(builder.CreateAnd(val2, 0x3f), builder.getInt64Ty())));
				break;

			case J3Cst::BC_iushr:                         /* 0x7c */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateLShr(val1, builder.CreateAnd(val2, 0x1f)));
				break;

			case J3Cst::BC_lushr:                         /* 0x7d */
				val2 = stack.pop(); val1 = stack.pop(); 
				stack.push(builder.CreateLShr(val1, builder.CreateZExt(builder.CreateAnd(val2, 0x3f), builder.getInt64Ty())));
				break;

			case J3Cst::BC_iand:                          /* 0x7e */
			case J3Cst::BC_land:                          /* 0x7f */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateAnd(val1, val2));
				break;

			case J3Cst::BC_ior:                           /* 0x80 */
			case J3Cst::BC_lor:                           /* 0x81 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateOr(val1, val2));
				break;

			case J3Cst::BC_ixor:                          /* 0x82 */
			case J3Cst::BC_lxor:                          /* 0x83 */
				val2 = stack.pop(); val1 = stack.pop(); stack.push(builder.CreateXor(val1, val2));
				break;

			case J3Cst::BC_iinc:                          /* 0x84 wide */
				{ uint32_t idx = wideReadU1(); 
					int32_t  val = wideReadS1(); 
					locals.setAt(builder.CreateAdd(locals.at(idx, vm->typeInteger->llvmType()), builder.getInt32(val)), idx);
				} break;

			case J3Cst::BC_i2l:                           /* 0x85 */
				stack.push(builder.CreateSExt(stack.pop(), vm->typeLong->llvmType()));
				break;

			case J3Cst::BC_i2f:                           /* 0x86 */
				stack.push(builder.CreateSIToFP(stack.pop(), vm->typeFloat->llvmType()));
				break;

			case J3Cst::BC_i2d:                           /* 0x87 */
				stack.push(builder.CreateSIToFP(stack.pop(), vm->typeDouble->llvmType()));
				break;

			case J3Cst::BC_l2i:                           /* 0x88 */
				stack.push(builder.CreateTruncOrBitCast(stack.pop(), builder.getInt32Ty()));
				break;

			case J3Cst::BC_l2f:                           /* 0x89 */
				stack.push(builder.CreateSIToFP(stack.pop(), vm->typeFloat->llvmType()));
				break;

			case J3Cst::BC_l2d:                           /* 0x8a */
				stack.push(builder.CreateSIToFP(stack.pop(), vm->typeDouble->llvmType()));
				break;

			case J3Cst::BC_f2i:                           /* 0x8b */
				floatToInteger(vm->typeFloat, vm->typeInteger);
				break;

			case J3Cst::BC_f2l:                           /* 0x8c */
				floatToInteger(vm->typeFloat, vm->typeLong);
				break;

			case J3Cst::BC_f2d:                           /* 0x8d */
				stack.push(builder.CreateFPExt(stack.pop(), vm->typeDouble->llvmType()));
				break;

			case J3Cst::BC_d2i:                           /* 0x8e */
				floatToInteger(vm->typeDouble, vm->typeInteger);
				break;

			case J3Cst::BC_d2l:                           /* 0x8f */
				floatToInteger(vm->typeDouble, vm->typeLong);
				break;

			case J3Cst::BC_d2f:                           /* 0x90 */
				stack.push(builder.CreateFPTrunc(stack.pop(), vm->typeFloat->llvmType()));
				break;

			case J3Cst::BC_i2b:                           /* 0x91 */
				stack.push(builder.CreateSExt(builder.CreateTrunc(stack.pop(), builder.getInt8Ty()), builder.getInt32Ty()));
				break;

			case J3Cst::BC_i2c:                           /* 0x92 */
				stack.push(builder.CreateZExt(builder.CreateTrunc(stack.pop(), builder.getInt16Ty()), builder.getInt32Ty()));
				break;

			case J3Cst::BC_i2s:                           /* 0x93 */
				stack.push(builder.CreateSExt(builder.CreateTrunc(stack.pop(), builder.getInt16Ty()), builder.getInt32Ty()));
				break;

			case J3Cst::BC_lcmp:                          /* 0x94 */
				compareLong();
				break;

			case J3Cst::BC_fcmpl:                         /* 0x95 */
				compareFP(1);
				break;

			case J3Cst::BC_fcmpg:                         /* 0x96 */
				compareFP(0);
				break;

			case J3Cst::BC_dcmpl:                         /* 0x97 */
				compareFP(0);
				break;

			case J3Cst::BC_dcmpg:                         /* 0x98 */
				compareFP(1);
				break;

			case J3Cst::BC_ifeq:                          /* 0x99 */
				condBr(builder.CreateICmpEQ(stack.pop(), builder.getInt32(0)));
				break;

			case J3Cst::BC_ifne:                          /* 0x9a */
				condBr(builder.CreateICmpNE(stack.pop(), builder.getInt32(0)));
				break;

			case J3Cst::BC_iflt:                          /* 0x9b */
				condBr(builder.CreateICmpSLT(stack.pop(), builder.getInt32(0)));
				break;

			case J3Cst::BC_ifge:                          /* 0x9c */
				condBr(builder.CreateICmpSGE(stack.pop(), builder.getInt32(0)));
				break;

			case J3Cst::BC_ifgt:                          /* 0x9d */
				condBr(builder.CreateICmpSGT(stack.pop(), builder.getInt32(0)));
				break;

			case J3Cst::BC_ifle:                          /* 0x9e */
				condBr(builder.CreateICmpSLE(stack.pop(), builder.getInt32(0)));
				break;

			case J3Cst::BC_if_icmpeq:                     /* 0x9f */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpEQ(val1, val2));
				break;

			case J3Cst::BC_if_icmpne:                     /* 0xa0 */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpNE(val1, val2));
				break;

			case J3Cst::BC_if_icmplt:                     /* 0xa1 */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpSLT(val1, val2));
				break;

			case J3Cst::BC_if_icmpge:                     /* 0xa2 */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpSGE(val1, val2));
				break;

			case J3Cst::BC_if_icmpgt:                     /* 0xa3 */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpSGT(val1, val2));
				break;

			case J3Cst::BC_if_icmple:                     /* 0xa4 */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpSLE(val1, val2));
				break;

			case J3Cst::BC_if_acmpeq:                     /* 0xa5 */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpEQ(val1, val2));
				break;

			case J3Cst::BC_if_acmpne:                     /* 0xa6 */
				val2 = stack.pop(); val1 = stack.pop(); condBr(builder.CreateICmpNE(val1, val2));
				break;

			case J3Cst::BC_goto:                          /* 0xa7 */
				builder.CreateBr(forwardBranch("goto", javaPC + codeReader->readS2(), 0, 1));
				_onEndPoint();
				break;

			case J3Cst::BC_jsr: nyi();                    /* 0xa8 */
			case J3Cst::BC_ret: /* wide */ nyi();         /* 0xa9 */
			case J3Cst::BC_tableswitch:                   /* 0xaa */
				tableSwitch();
				_onEndPoint();
				break;

			case J3Cst::BC_lookupswitch: 
				lookupSwitch();
				_onEndPoint();
				break;

			case J3Cst::BC_ireturn:                       /* 0xac */
			case J3Cst::BC_lreturn:                       /* 0xad */
			case J3Cst::BC_freturn:                       /* 0xae */
			case J3Cst::BC_dreturn:                       /* 0xaf */
			case J3Cst::BC_areturn:                       /* 0xb0 */
				ret.setAt(stack.pop(), 0);
				builder.CreateBr(bbRet);
				_onEndPoint();
				break;

			case J3Cst::BC_return:                        /* 0xb1 */
				builder.CreateBr(bbRet);
				_onEndPoint();
				break;

			case J3Cst::BC_getstatic:                     /* 0xb2 */
				getStatic(codeReader->readU2());
				break;

			case J3Cst::BC_putstatic:                     /* 0xb3 */
				putStatic(codeReader->readU2());
				break;

			case J3Cst::BC_getfield:                      /* 0xb4 */
				getField(codeReader->readU2());
				break;

			case J3Cst::BC_putfield:                      /* 0xb5 */
				putField(codeReader->readU2());
				break;

			case J3Cst::BC_invokevirtual:                 /* 0xb6 */
				invokeVirtual(codeReader->readU2());
				break;

			case J3Cst::BC_invokespecial:                 /* 0xb7 */
				invokeSpecial(codeReader->readU2());
				break;

			case J3Cst::BC_invokestatic:                  /* 0xb8 */
				invokeStatic(codeReader->readU2());
				break;

			case J3Cst::BC_invokeinterface:               /* 0xb9 */
				invokeInterface(codeReader->readU2());
				codeReader->readU2();
				break;

			case J3Cst::BC_new:                           /* 0xbb */
				newObject(cl->classAt(codeReader->readU2())->asClass());
				break;
				
			case J3Cst::BC_newarray:                      /* 0xbc */
				newArray(codeReader->readU1());
				break;

			case J3Cst::BC_anewarray:                     /* 0xbd */
				newArray(cl->classAt(codeReader->readU2())->getArray());
				break;

			case J3Cst::BC_arraylength:                   /* 0xbe */
				stack.push(arrayLength(stack.pop()));
				break;

			case J3Cst::BC_athrow:                        /* 0xbf */
				{
					llvm::Value* excp = builder.CreateBitCast(stack.pop(), funcThrowException->getFunctionType()->getParamType(0));
					if(exceptions.nodes[curExceptionNode]->landingPad)
						builder.CreateInvoke(funcThrowException, bbRet, exceptions.nodes[curExceptionNode]->landingPad, excp);
					else {
						builder.CreateCall(funcThrowException, excp);
						builder.CreateBr(bbRet);
					}
					_onEndPoint();
				}
				break;

			case J3Cst::BC_checkcast:                     /* 0xc0 */
				checkCast(stack.top(), cl->classAt(codeReader->readU2()));
				break;

			case J3Cst::BC_instanceof:                    /* 0xc1 */
				instanceof(stack.pop(), cl->classAt(codeReader->readU2()));
				break;

			case J3Cst::BC_monitorenter:                  /* 0xc2 */
				monitorEnter(stack.pop());
				break;

			case J3Cst::BC_monitorexit:                   /* 0xc3 */
				monitorExit(stack.pop());
				break;

			case J3Cst::BC_wide:                          /* 0xc4 */
				isWide = 1;
				break;

			case J3Cst::BC_multianewarray:                /* 0xc5 */
				multianewArray();
				break;

			case J3Cst::BC_ifnull:                        /* 0xc6 */
				condBr(builder.CreateIsNull(stack.pop()));
				break;

			case J3Cst::BC_ifnonnull:                     /* 0xc7 */
				condBr(builder.CreateIsNotNull(stack.pop()));
				break;


			case J3Cst::BC_goto_w: nyi();                 /* 0xc8 */
			case J3Cst::BC_jsr_w: nyi();                  /* 0xc9 */

			case J3Cst::BC_breakpoint:                    /* 0xca */
			case J3Cst::BC_impdep1:                       /* 0xfe */
			case J3Cst::BC_impdep2:                       /* 0xff */
			case J3Cst::BC_xxxunusedxxx1:                 /* 0xba */
			default:
				J3::classFormatError(cl, "unknow opcode '%s' (%d)", J3Cst::opcodeNames[bc], bc);
		}
	}
	J3::classFormatError(cl, "the last bytecode does not return");
}

#if 0
void J3CodeGen::explore() {
	printf("  exploring bytecode of: %s::%s%s\n", cl->name()->cStr(), method->name()->cStr(), method->signature()->cStr());
	while(codeReader->remaining()) {
		bc = codeReader->readU1();

		printf("    exploring: %s (%d)\n", J3Cst::opcodeNames[bc], bc);
		switch(bc) {
#define eat(n) codeReader->seek(n, codeReader->SeekCur);
#define defOpcode(id, val, effect)							\
			case J3Cst::BC_##id: effect; break;
#include "j3/j3bc.def"
			default:
				J3::internalError("unknow opcode '%s' (%d)", J3Cst::opcodeNames[bc], bc);
		}
	}
}
#endif

void J3CodeGen::z_translate() {
	bbRet = newBB("ret");
	llvm::BasicBlock* landingPad = newBB("landing-pad");
	llvm::Value* val = builder.CreateIntToPtr(llvm::ConstantInt::get(uintPtrTy, (uintptr_t)0x42),
																						 vm->typeJ3ObjectPtr);	
	builder.CreateInvoke(funcThrowException, bbRet, landingPad,
												builder.CreateBitCast(val, funcThrowException->getFunctionType()->getParamType(0)));

	builder.SetInsertPoint(landingPad);
	llvm::LandingPadInst *caughtResult = builder.CreateLandingPad(vm->typeGXXException, 
																																 funcGXXPersonality, 
																																 1, 
																																 "landing-pad");
	caughtResult->addClause(gvTypeInfo);

	llvm::Value* excp = builder.CreateBitCast(builder.CreateCall(funcCXABeginCatch, 
																																 builder.CreateExtractValue(caughtResult, 0)),
																						 vm->typeJ3ObjectPtr);
	
	builder.CreateCall(funcCXAEndCatch);

	builder.CreateCall3(funcEchoDebugExecute, 
											 builder.getInt32(-1), /* just to see my first exception :) */
											 buildString("catching exception %p!\n"),
											 excp);
	builder.CreateBr(bbRet);

	builder.SetInsertPoint(bbRet);
	builder.CreateRetVoid();

	llvmFunction->dump();
}

void J3CodeGen::generateJava() {
	llvm::BasicBlock* entry = newBB("entry");
	builder.SetInsertPoint(entry);

	J3Attribute* attr = method->attributes()->lookup(vm->codeAttribute);

	if(!attr)
		J3::classFormatError(cl, "No Code attribute in %s %s", method->name()->cStr(), method->signature()->name()->cStr());

	J3Reader reader(cl->bytes());
	reader.seek(attr->offset(), reader.SeekSet);

	uint32_t length = reader.readU4();
	
	if(!reader.adjustSize(length))
		J3::classFormatError(cl, "Code attribute of %s %s is too large (%d)", 
												 method->name()->cStr(), 
												 method->signature()->name()->cStr(), 
												 length);

	llvm::DIBuilder* dbgBuilder = new llvm::DIBuilder(*module);

  dbgInfo =
		dbgBuilder->createFunction(llvm::DIDescriptor(),    // Function scope
															 llvmFunction->getName(), // Function name
															 llvmFunction->getName(), // Mangled name
															 llvm::DIFile(),          // File where this variable is defined
															 0,                       // Line number
															 dbgBuilder     // Function type.
																				 ->createSubroutineType(llvm::DIFile(),       // File in which this subroutine is defined 
																																llvm::DIArray()),     // An array of subroutine parameter types. 
																				                                              // This includes return type at 0th index.
															 false,                   // True if this function is not externally visible
															 false,                   // True if this is a function definition
															 0                        // Set to the beginning of the scope this starts
															 );

	uint32_t maxStack   = reader.readU2();
	uint32_t nbLocals  = reader.readU2();
	uint32_t codeLength = reader.readU4();

	locals.init(this, nbLocals);
	stack.init(this, maxStack);
	ret.init(this, 1);

	genDebugEnterLeave(0);

	uint32_t n=0, pos=0;
	for(llvm::Function::arg_iterator cur=llvmFunction->arg_begin(); cur!=llvmFunction->arg_end(); cur++) {
		locals.setAt(flatten(cur), pos);
		pos += (cur->getType() == vm->typeLong->llvmType() || cur->getType() == vm->typeDouble->llvmType()) ? 2 : 1;
	}

	//builder.CreateCall(ziTry);

	pendingBranchs = (uint32_t*)allocator->allocate(sizeof(uint32_t) * codeLength);
	opInfos = (J3OpInfo*)allocator->allocate(sizeof(J3OpInfo) * codeLength);
	memset(opInfos, 0, sizeof(J3OpInfo) * codeLength);

	J3Reader codeReaderTmp(cl->bytes(), reader.tell(), codeLength);
	codeReader = &codeReaderTmp;

	bbRet = newBB("ret");
	builder.SetInsertPoint(bbRet);

	genDebugEnterLeave(1);

	if(llvmFunction->getReturnType()->isVoidTy())
		builder.CreateRetVoid();
	else
		builder.CreateRet(unflatten(ret.at(0, llvmFunction->getReturnType()), llvmFunction->getReturnType()));

	if(J3Cst::isSynchronized(method->access())) {
		static bool echoDone = 0;
		if(!echoDone) {
			fprintf(stderr, "IMPLEMENT ME: synchronized java\n");
			echoDone = 1;
		}
	}

	reader.seek(codeLength, reader.SeekCur);

	exceptions.read(&reader, codeLength);

	pendingBranchs[topPendingBranchs++] = codeReader->tell();

	builder.SetInsertPoint(entry);

	translate();

	locals.killUnused();
	stack.killUnused();
	ret.killUnused();
}

llvm::Type* J3CodeGen::doNativeType(llvm::Type* type) {
	return type->isPointerTy() ? vm->typeJ3ObjectHandlePtr : type;
}

llvm::Function* J3CodeGen::lookupNative() {
	J3Mangler      mangler(cl);

	mangler.mangle(mangler.javaId)->mangle(cl->name(), method->name());
	uint32_t length = mangler.length();
	mangler.mangle(method->signature());

	void* fnPtr = method->nativeFnPtr();

	if(!fnPtr)
		fnPtr = loader->lookupNativeFunctionPointer(method, mangler.cStr());

	if(!fnPtr) {
		mangler.cStr()[length] = 0;
		fnPtr = loader->lookupNativeFunctionPointer(method, mangler.cStr());
	}

	if(!fnPtr)
		J3::linkageError(method);

	std::vector<llvm::Type*> nativeIns;
	llvm::Type*              nativeOut;

	nativeIns.push_back(vm->typeJNIEnvPtr);

	if(J3Cst::isStatic(method->access()))
		nativeIns.push_back(doNativeType(vm->classClass->llvmType()));

	llvm::FunctionType*      origFType = method->signature()->functionType(method->access());
	for(llvm::FunctionType::param_iterator it=origFType->param_begin(); it!=origFType->param_end(); it++)
		nativeIns.push_back(doNativeType(*it));

	nativeOut = doNativeType(origFType->getReturnType());

	char* buf = (char*)loader->allocator()->allocate(mangler.length()+1);
	memcpy(buf, mangler.cStr(), mangler.length()+1);

	llvm::FunctionType* fType = llvm::FunctionType::get(nativeOut, nativeIns, 0);
	llvm::Function* res = llvm::Function::Create(fType,
																							 llvm::GlobalValue::ExternalLinkage,
																							 buf,
																							 module);

	loader->addSymbol(buf, new(loader->allocator()) vmkit::NativeSymbol(fnPtr));

	return res;
}

void J3CodeGen::generateNative() {
	builder.SetInsertPoint(newBB("entry"));

	std::vector<llvm::Value*> args;

	llvm::Function* nat = lookupNative();

	llvm::Value* res;
	llvm::Value* thread = currentThread();
	llvm::Value* frame = builder.CreateCall(funcJ3ThreadTell, thread);

	if(J3Cst::isSynchronized(method->access())) {
		static bool echoDone = 0;
		if(!echoDone) {
			fprintf(stderr, "IMPLEMENT ME: synchronized java\n");
			echoDone = 1;
		}
	}

	args.push_back(builder.CreateCall(funcJniEnv));
	if(J3Cst::isStatic(method->access()))
		args.push_back(javaClass(cl, 1));

	uint32_t selfDone = 0;

	for(llvm::Function::arg_iterator cur=llvmFunction->arg_begin(); cur!=llvmFunction->arg_end(); cur++) {
		llvm::Value* v = cur;
		args.push_back(v->getType()->isPointerTy() ?
									 builder.CreateCall2(funcJ3ThreadPush, thread, v) :
									 v);
	}

	res = builder.CreateCall(nat, args);
	builder.CreateCall(funcReplayException);

	if(llvmFunction->getReturnType()->isVoidTy()) {
		builder.CreateCall2(funcJ3ThreadRestore, thread, frame);
		builder.CreateRetVoid();
	} else {
		if(llvmFunction->getReturnType()->isPointerTy()) {
			llvm::BasicBlock* ifnull = newBB("ifnull");
			llvm::BasicBlock* ifnotnull = newBB("ifnotnull");
			builder.CreateCondBr(builder.CreateIsNull(res), ifnull, ifnotnull);

			builder.SetInsertPoint(ifnull);
			builder.CreateCall2(funcJ3ThreadRestore, thread, frame);
			builder.CreateRet(nullValue);

			builder.SetInsertPoint(ifnotnull);
			res = handleToObject(res);
			builder.CreateCall2(funcJ3ThreadRestore, thread, frame);
		}
		builder.CreateRet(res);
	}
}
