//===----------- JavaThread.h - Java thread description -------------------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_JAVA_THREAD_H
#define JNJVM_JAVA_THREAD_H

#include "mvm/Object.h"
#include "mvm/Threads/Cond.h"
#include "mvm/Threads/Locks.h"
#include "mvm/Threads/Thread.h"

#include "MutatorThread.h"

#include "JavaObject.h"
#include "JNIReferences.h"

namespace j3 {

class Class;
class JavaMethod;
class JavaObject;
class Jnjvm;


#define BEGIN_NATIVE_EXCEPTION(level)						\
  JavaThread* __th = JavaThread::get();					\
  TRY {

#define END_NATIVE_EXCEPTION										\
  } CATCH {																			\
    __th->throwFromNative();										\
  } END_CATCH;

#define BEGIN_JNI_EXCEPTION														\
  mvm::Thread* mut = mvm::Thread::get();							\
  void* SP = mut->getLastSP();												\
  mut->leaveUncooperativeCode();											\
  mvm::KnownFrame Frame;															\
  mut->startKnownFrame(Frame);												\
  TRY {

#define END_JNI_EXCEPTION													\
  } CATCH {																				\
		JavaThread::j3Thread(mut)->throwFromJNI(SP);	\
  } END_CATCH;

#define RETURN_FROM_JNI(a) {											\
		mut->endKnownFrame();													\
		mut->enterUncooperativeCode(SP);							\
		return (a); }																	\

#define RETURN_VOID_FROM_JNI {										\
		mut->endKnownFrame();													\
		mut->enterUncooperativeCode(SP);							\
		return; }																			\


/// JavaThread - This class is the internal representation of a Java thread.
/// It maintains thread-specific information such as its state, the current
/// exception if there is one, the layout of the stack, etc.
///
class JavaThread : public mvm::MutatorThread {
public:
  
  /// jniEnv - The JNI environment of the thread.
  ///
  void* jniEnv;
  
  /// pendingException - The Java exception currently pending.
  ///
  JavaObject* pendingException;

  /// javaThread - The Java representation of this thread.
  ///
  JavaObject* javaThread;

  /// mut - The associated mutator. Should be removed
	mvm::Thread* mut;

  /// vmThread - The VMThread object of this thread.
  ///
  JavaObject* vmThread;

  /// varcond - Condition variable when the thread needs to be awaken from
  /// a wait.
  ///
  mvm::Cond varcond;

  /// interruptFlag - Has this thread been interrupted?
  ///
  uint32 interruptFlag;

  /// nextWaiting - Next thread waiting on the same monitor.
  ///
  JavaThread* nextWaiting;
  
  /// prevWaiting - Previous thread waiting on the same monitor.
  ///
  JavaThread* prevWaiting;

  /// waitsOn - The monitor on which the thread is waiting on.
  ///
  JavaLock* waitsOn;

  static const unsigned int StateRunning;
  static const unsigned int StateWaiting;
  static const unsigned int StateInterrupted;

  /// state - The current state of this thread: Running, Waiting or Interrupted.
  uint32 state;
  
  /// currentAddedReferences - Current number of added local references.
  ///
  uint32_t* currentAddedReferences;

  /// localJNIRefs - List of local JNI references.
  ///
  JNILocalReferences* localJNIRefs;

  /// jnjvm - my vm
  ///
	Jnjvm *jnjvm;

  JavaObject** pushJNIRef(JavaObject* obj) {
    llvm_gcroot(obj, 0);
    if (!obj) return 0;
   
    ++(*currentAddedReferences);
    return localJNIRefs->addJNIReference(this, obj);

  }

  /// tracer - Traces GC-objects pointed by this thread object.
  ///
  virtual void tracer(uintptr_t closure);

  /// JavaThread - Empty constructor, used to get the VT.
  ///
  JavaThread() {
#ifdef SERVICE
    replacedEIPs = 0;
#endif
  }

  /// ~JavaThread - Delete any potential malloc'ed objects used by this thread.
  ///
  ~JavaThread();

private:  
  /// JavaThread - Creates a Java thread.
  ///
  JavaThread(JavaObject* thread, JavaObject* vmThread, Jnjvm* isolate);

public:
	static mvm::Thread* create(JavaObject* thread, JavaObject* vmThread, Jnjvm* isolate);
	static JavaThread*  j3Thread(mvm::Thread* mut);

  /// get - Get the current thread as a JnJVM object.
  ///
  static JavaThread* get() {
    return j3Thread(mvm::Thread::get());
  }

  /// getJVM - Get the JnJVM in which this thread executes.
  ///
  Jnjvm* getJVM() {
    return jnjvm;
  }

  /// currentThread - Return the current thread as a Java object.
  ///
  JavaObject* currentThread() {
    return javaThread;
  }
 
  /// throwException - Throw the given exception in the current thread.
  ///
  void throwException(JavaObject* obj);

  /// throwPendingException - Throw a pending exception.
  ///
  void throwPendingException();
  
  /// getJavaException - Return the pending exception.
  ///
  JavaObject* getJavaException() {
    return pendingException;
  }

  /// throwFromJNI - Throw an exception after executing JNI code.
  ///
  void throwFromJNI(void* SP) {
    endKnownFrame();
    enterUncooperativeCode(SP);
  }
  
  /// throwFromNative - Throw an exception after executing Native code.
  ///
  void throwFromNative() {
#ifdef DWARF_EXCEPTIONS
    throwPendingException();
#endif
  }
  
  /// throwFromJava - Throw an exception after executing Java code.
  ///
  void throwFromJava() {
    throwPendingException();
  }

  /// startJava - Interesting, but actually does nothing :)
  void startJava() {}
  
  /// endJava - Interesting, but actually does nothing :)
  void endJava() {}

  /// startJNI - Record that we are entering native code.
  ///
  void startJNI(int level) __attribute__ ((noinline));

  void endJNI() {
    localJNIRefs->removeJNIReferences(this, *currentAddedReferences);
   
    // Go back to cooperative mode.
    leaveUncooperativeCode();
   
    endKnownFrame();
  }

  /// getCallingMethod - Get the Java method in the stack at the specified
  /// level.
  ///
  JavaMethod* getCallingMethodLevel(uint32 level);
  
  /// getCallingClassLevel - Get the Java method in the stack at the
  /// specified level.
  ///
  UserClass* getCallingClassLevel(uint32 level);
  
  /// getNonNullClassLoader - Get the first non-null class loader on the
  /// stack.
  ///
  JavaObject* getNonNullClassLoader();
    
  /// printJavaBacktrace - Prints the backtrace of this thread. Only prints
  /// the Java methods on the stack.
  ///
  void printJavaBacktrace();

  /// getJavaFrameContext - Fill the buffer with Java methods currently on
  /// the stack.
  ///
  uint32 getJavaFrameContext(void** buffer);
  
private:
  /// internalClearException - Clear the C++ and Java exceptions
  /// currently pending.
  ///
  virtual void internalClearException() {
    pendingException = NULL;
  }

public:

#ifdef SERVICE
  /// ServiceException - The exception that will be thrown if a bundle is
  /// stopped.
  JavaObject* ServiceException;

  /// replacedEIPs - List of instruction pointers which must be replaced
  /// to a function that throws an exception. We maintain this list and update
  /// the stack correctly so that Dwarf unwinding does not complain.
  ///
  void** replacedEIPs;

  /// eipIndex - The current index in the replacedIPs list.
  ///
  uint32_t eipIndex;
#endif

};

} // end namespace j3

#endif
