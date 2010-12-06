#ifndef _VMKIT_H_
#define _VMKIT_H_

#include "mvm/Allocator.h"
#include "mvm/Threads/CollectionRV.h"
#include "mvm/VirtualMachine.h"

namespace mvm {
class MethodInfo;
class VMKit;
class gc;
class FinalizerThread;

class FunctionMap {
public:
  /// Functions - Map of applicative methods to function pointers. This map is
  /// used when walking the stack so that VMKit knows which applicative method
  /// is executing on the stack.
  ///
  std::map<void*, MethodInfo*> Functions;

  /// FunctionMapLock - Spin lock to protect the Functions map.
  ///
  mvm::SpinLock FunctionMapLock;

  /// IPToMethodInfo - Map a code start instruction instruction to the MethodInfo.
  ///
  MethodInfo* IPToMethodInfo(void* ip);

  /// addMethodInfo - A new instruction pointer in the function map.
  ///
  void addMethodInfo(MethodInfo* meth, void* ip);

  /// removeMethodInfos - Remove all MethodInfo owned by the given owner.
  void removeMethodInfos(void* owner);

  FunctionMap();
};

class VMKit : public mvm::PermanentObject {
public:
  /// allocator - Bump pointer allocator to allocate permanent memory of VMKit
  mvm::BumpPtrAllocator& allocator;

  VMKit(mvm::BumpPtrAllocator &Alloc);

	LockNormal                   _vmkitLock;

	void vmkitLock() { _vmkitLock.lock(); }
	void vmkitUnlock() { _vmkitLock.unlock(); }

	/// ------------------------------------------------- ///
	/// ---             vm managment                  --- ///
	/// ------------------------------------------------- ///
	// vms - the list of vms. 
	//       synchronized with vmkitLock
	VirtualMachine**             vms;
	size_t                       vmsArraySize;

	size_t addVM(VirtualMachine* vm);
	void   removeVM(size_t id);

	/// ------------------------------------------------- ///
	/// ---             thread managment              --- ///
	/// ------------------------------------------------- ///
  /// preparedThreads - the list of prepared threads, they are not yet running.
	///                   synchronized with vmkitLock
  ///
	CircularBase<Thread>         preparedThreads;

  /// runningThreads - the list of running threads
	///                  synchronize with vmkitLock
  ///
	CircularBase<Thread>         runningThreads;

  /// numberOfRunningThreads - The number of threads that currently run under this VM.
	///                          synchronized with vmkitLock
	///
  size_t                       numberOfRunningThreads;

  /// rendezvous - The rendezvous implementation for garbage collection.
  ///
#ifdef WITH_LLVM_GCC
  CooperativeCollectionRV      rendezvous;
#else
  UncooperativeCollectionRV    rendezvous;
#endif

	void registerPreparedThread(mvm::Thread* th);  
	void unregisterPreparedThread(mvm::Thread* th);

	void registerRunningThread(mvm::Thread* th);  
	void unregisterRunningThread(mvm::Thread* th);

	FinalizerThread*             finalizerThread;

  /// scanFinalizationQueue - Scan objets with a finalized method and schedule
  /// them for finalization if they are not live.
  /// 
  void scanFinalizationQueue(uintptr_t closure);

  /// addFinalizationCandidate - Add an object to the queue of objects with
  /// a finalization method.
  ///
  void addFinalizationCandidate(gc* object);

	/// ------------------------------------------------- ///
	/// ---             collection managment          --- ///
	/// ------------------------------------------------- ///

	bool startCollection(); // 1 ok, begin collection, 0 do not start collection
	void endCollection();

  void tracer(uintptr_t closure);

	/// ------------------------------------------------- ///
	/// ---    backtrace related methods              --- ///
	/// ------------------------------------------------- ///
  /// FunctionsCache - cache of compiled functions
	//  
  FunctionMap FunctionsCache;

  MethodInfo* IPToMethodInfo(void* ip) {
    return FunctionsCache.IPToMethodInfo(ip);
  }

  void removeMethodInfos(void* owner) {
    FunctionsCache.removeMethodInfos(owner);
  }
};

}

#endif
