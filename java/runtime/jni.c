#include "runtime.h"
#include <stdlib.h>
#include <string.h>

/* The implementation of JNI functions */

static jclass find_class(JNIEnv* env, const char* name) {
  return GET_CLASS(llvm_java_find_class_record(name));
}

static jboolean is_assignable_from(JNIEnv* env, jclass c1, jclass c2) {
  return llvm_java_is_assignable_from(GET_CLASS_RECORD(c1),
                                      GET_CLASS_RECORD(c2));
}

static jboolean is_same_object(JNIEnv* env, jobject o1, jobject o2) {
  return o1 == o2;
}

static jclass get_object_class(JNIEnv* env, jobject obj) {
  return GET_CLASS(llvm_java_get_class_record(obj));
}

static jboolean is_instance_of(JNIEnv* env, jobject obj, jclass c) {
  return llvm_java_is_instance_of(obj, GET_CLASS_RECORD(c));
}

static jint get_array_length(JNIEnv* env, jarray array) {
  return ((struct llvm_java_booleanarray*) array)->length;
}

#define HANDLE_NATIVE_TYPE(TYPE) \
  static j ## TYPE* get_##TYPE##_array_elements( \
    JNIEnv* env, \
    jarray array, \
    jboolean* isCopy) { \
    if (isCopy) \
      *isCopy = JNI_FALSE; \
    return ((struct llvm_java_ ##TYPE## array*) array)->data; \
  }
#include "types.def"

#define HANDLE_NATIVE_TYPE(TYPE) \
  static void release_ ##TYPE## _array_elements( \
    JNIEnv* env, \
    jarray array, \
    j##TYPE* elements, \
    jint mode) { \
    switch (mode) { \
    case 0: \
    case JNI_COMMIT: \
    case JNI_ABORT: \
      return; \
    default: \
      abort(); \
    } \
  }
#include "types.def"

/* The JNI interface definition */
static const struct JNINativeInterface llvm_java_JNINativeInterface = {
  NULL, /* 0 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  &find_class,
  NULL,
  NULL,
  NULL,
  NULL, /* 10 */
  &is_assignable_from,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 20 */
  NULL,
  NULL,
  NULL,
  &is_same_object,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 30 */
  &get_object_class,
  &is_instance_of,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 40 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 50 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 60 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 70 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 80 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 90 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 100 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 110 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 120 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 130 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 140 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 150 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 160 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 170 */
  &get_array_length,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 180 */
  NULL,
  NULL,
  &get_boolean_array_elements,
  &get_byte_array_elements,
  &get_char_array_elements,
  &get_short_array_elements,
  &get_int_array_elements,
  &get_long_array_elements,
  &get_float_array_elements,
  &get_double_array_elements,
  &release_boolean_array_elements,
  &release_byte_array_elements,
  &release_char_array_elements,
  &release_short_array_elements,
  &release_int_array_elements,
  &release_long_array_elements,
  &release_float_array_elements,
  &release_double_array_elements,
  NULL,
  NULL, /* 200 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 210 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 220 */
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* 230 */
  NULL,
};

const JNIEnv llvm_java_JNIEnv = &llvm_java_JNINativeInterface;
