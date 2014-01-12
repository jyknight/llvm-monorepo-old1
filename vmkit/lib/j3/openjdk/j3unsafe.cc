#include "j3/j3jni.h"
#include "j3/j3object.h"
#include "j3/j3thread.h"
#include "j3/j3class.h"
#include "j3/j3classloader.h"
#include "j3/j3field.h"
#include "j3/j3.h"

#include "llvm/IR/DataLayout.h"

using namespace j3;

extern "C" {
	JNIEXPORT void JNICALL Java_sun_misc_Unsafe_registerNatives(JNIEnv* env, jclass clazz) {
		// Nothing, we define the Unsafe methods with the expected signatures.
	}

	JNIEXPORT void JNICALL Java_sun_misc_Unsafe_ensureClassInitialized(JNIEnv* env, jobject unsafe, jclass clazz) {
		enterJVM(); 
		J3ObjectType::nativeClass(clazz)->initialise();
		leaveJVM(); 
	}

	/// arrayBaseOffset - Offset from the array object where the actual
	/// element data begins.
	///
	JNIEXPORT jlong JNICALL Java_sun_misc_Unsafe_arrayBaseOffset(JNIEnv* env, jobject unsafe, jclass clazz) {
		return sizeof(J3ArrayObject);
	}

	/// arrayIndexScale - Indexing scale for the element type in
	/// the specified array.  For use with arrayBaseOffset,
	/// NthElementPtr = ArrayObject + BaseOffset + N*IndexScale
	/// Return '0' if our JVM stores the elements in a way that
	/// makes this type of access impossible or unsupported.
	///
	JNIEXPORT jlong JNICALL Java_sun_misc_Unsafe_arrayIndexScale(JNIEnv* env, jobject unsafe, jclass clazz) {
		jlong res;
		enterJVM(); 
		res = 1<<J3ObjectType::nativeClass(clazz)->asArrayClass()->component()->logSize();
		leaveJVM(); 
		return res;
	}

	JNIEXPORT jint JNICALL Java_sun_misc_Unsafe_addressSize(JNIEnv* env, jobject unsafe) {
		jint res;
		enterJVM(); 
		res = J3Thread::get()->vm()->objectClass->getSizeInBits()>>3;
		leaveJVM(); 
		return res;
	}

	/// objectFieldOffset - Pointer offset of the specified field
	///
	JNIEXPORT jlong JNICALL Java_sun_misc_Unsafe_objectFieldOffset(JNIEnv* env, jobject unsafe, jobject field) {
		jlong res;
		enterJVM(); 
		J3* vm = J3Thread::get()->vm();
		J3Class* cl = J3Class::nativeClass(field->getObject(vm->fieldClassClass))->asClass();
		uint32_t slot = field->getInteger(vm->fieldClassSlot);
		uint32_t access = field->getInteger(vm->fieldClassAccess);
		J3Field* fields = J3Cst::isStatic(access) ? cl->staticLayout()->fields() : cl->fields();
		res = fields[slot].offset();
		leaveJVM(); 
		return res;
	}

	JNIEXPORT jlong JNICALL Java_sun_misc_Unsafe_allocateMemory(JNIEnv* env, jobject unsafe, jlong bytes) {
		jlong res;
		enterJVM();
		res = (jlong)(uintptr_t)malloc(bytes); 
		if(!res)
			J3::outOfMemoryError();
		leaveJVM();
		return res;
	}

	JNIEXPORT void JNICALL Java_sun_misc_Unsafe_freeMemory(JNIEnv* env, jobject unsafe, jlong addr) {
		enterJVM();
		free((void*)(uintptr_t)addr);
		leaveJVM();
	}

#define unsafeGetPut(jtype, id, j3id, sign)															\
	JNIEXPORT void JNICALL Java_sun_misc_Unsafe_put##id##__J##sign(JNIEnv* env, jobject unsafe, jlong addr, jtype value) { \
		*(jtype*)(uintptr_t)addr = value;																		\
	}																																			\
																																				\
	JNIEXPORT jtype JNICALL Java_sun_misc_Unsafe_get##id##__J(JNIEnv* env, jobject unsafe, jlong addr) { \
		return *(jtype*)(uintptr_t)addr;																		\
	}

#define unsafeCAS(jtype, id, j3id, sign)																\
	JNIEXPORT bool JNICALL Java_sun_misc_Unsafe_compareAndSwap##id(JNIEnv* env, jobject unsafe, \
																																 jobject handle, jlong offset, jtype orig, jtype value) { \
		bool res;																														\
		enterJVM();																													\
		res = handle->rawCAS##j3id(offset, orig, value) == orig;						\
		leaveJVM();																													\
		return res;																													\
	}

#define unsafeGetVolatile(jtype, id, j3id, sign)												\
	JNIEXPORT jtype JNICALL Java_sun_misc_Unsafe_get##id##Volatile(JNIEnv* env, jobject unsafe, jobject handle, jlong offset) { \
		jtype res;																													\
		enterJVM();																													\
		res =  handle->rawGet##j3id(offset);																\
		leaveJVM();																													\
		return res;																													\
	}

#define defUnsafe(jtype, id, j3id, sign)								\
	unsafeGetVolatile(jtype, id, j3id, sign)							\
	unsafeCAS(jtype, id, j3id, sign)											\
	unsafeGetPut(jtype, id, j3id, sign)

	defUnsafe(jobject,  Object,  Object,    Ljava_lang_Object_2);
	defUnsafe(jboolean, Boolean, Boolean,   Z);
	defUnsafe(jbyte,    Byte,    Byte,      B);
	defUnsafe(jchar,    Char,    Character, C);
	defUnsafe(jshort,   Short,   Short,     S);
	defUnsafe(jint,     Int,     Integer,   I);
	defUnsafe(jlong,    Long,    Long,      J);
	defUnsafe(jfloat,   Float,   Float,     F);
	defUnsafe(jdouble,  Double,  Double,    D);
}




