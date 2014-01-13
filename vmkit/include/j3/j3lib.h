#ifndef _J3_LIB_H_
#define _J3_LIB_H_

#include <vmkit/allocator.h>
#include <vector>

namespace j3 {
	class J3;
	class J3ClassLoader;
	class J3ObjectHandle;
	class J3Options;

	class J3Lib {
	public:
		static void processOptions(J3* vm);
		static void loadSystemLibraries(J3ClassLoader* loader);
		static void bootstrap(J3* vm);

		static J3ObjectHandle* newDirectByteBuffer(void* address, size_t len);
	};
}

#endif
