#ifndef _J3_JNI_H_
#define _J3_JNI_H_

#include "jni.h"

namespace j3 {
	extern struct JNINativeInterface_ jniEnvTable;
	extern struct JNIInvokeInterface_ javaVMTable;
}

#define enterJVM() try {
#define leaveJVM() } catch(void* e) {																		\
	J3Thread::get()->setPendingException(J3Thread::get()->push((J3Object*)e)); \
 }

#endif
