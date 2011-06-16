#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define DEBUG (0)

/* Size of shadow memory.  We're hoping everything fits in 46bits. */
#define SIZE ((size_t)(1L << 46))

/* Fixed start of memory.  Needs to be page-aligned,
 * and it needs to be large enough that program itself is loaded below it
 * and so are the libraries. 
 * FIXME: This address has been picked for maute. Might not work on other
 * machines. Need a more robust way of picking base address.
 * For now, run a version of the tool without the base fixed, and 
 * choose address.
 #define BASE ((void *)(0x2aaaab2a5000))
 */
#define BASE ((void *)(0x2aaaab88c000))
/*
 * Do some macro magic to get mmap macros defined properly on all platforms.
 */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
# define MAP_ANONYMOUS MAP_ANON
#endif /* defined(MAP_ANON) && !defined(MAP_ANONYMOUS) */

uint8_t * const shadow_begin = BASE;

extern char* typeNames[];

void trackInitInst(void *ptr, uint64_t size, uint32_t tag);

inline uintptr_t maskAddress(void *ptr) {
  uintptr_t p = (uintptr_t)ptr;
  if (p >= (uintptr_t)BASE + SIZE) p -= SIZE;

#if DEBUG
  assert(p <= SIZE && "Pointer out of range!");
#endif
  return p;
}


void trackStringInput(void *ptr, uint32_t tag) {
  trackInitInst(ptr, strlen(ptr) + 1, tag);
}


/**
 * Initialize the shadow memory which records the 1:1 mapping of addresses to types.
 */
void shadowInit() {
  char * res = mmap(BASE, SIZE, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  if (res == MAP_FAILED) {
    fprintf(stderr, "Failed to map the shadow memory!\n");
    fflush(stderr);
    assert(0 && "MAP_FAILED");
  }
}

/**
 * Copy arguments into a new array, and initialize
 * metadata for that location to TOP/initialized.
 */
void trackArgvType(int argc, char **argv) {
  int index = 0;
  for (; index < argc; ++index) {
    trackInitInst(argv[index], (strlen(argv[index]) + 1)*sizeof(char), 0);
  }
  trackInitInst(argv, (argc + 1)*sizeof(char*), 0);
}

void trackEnvpType(char **envp) {
  int index = 0;
  for(;envp[index] != NULL; ++index)
    trackInitInst(envp[index], (strlen(envp[index]) + 1)*sizeof(char), 0);
  trackInitInst(envp, (index )*sizeof(char*), 0);
}

/**
 * Record the global type and address in the shadow memory.
 */
void trackGlobal(void *ptr, uint8_t typeNumber, uint64_t size, uint32_t tag) {
  uintptr_t p = maskAddress(ptr);
  shadow_begin[p] = typeNumber;
  memset(&shadow_begin[p + 1], 0, size - 1);
#if DEBUG
  printf("Global(%d): %p, %p = %u | %lu bytes\n", tag, ptr, (void *)p, typeNumber, size);
#endif
}

/**
 * Record the type stored at ptr(of size size) and replicate it
 */
void trackArray(void *ptr, uint64_t size, uint64_t count, uint32_t tag) {
  uintptr_t p = maskAddress(ptr);
  uintptr_t p1 = maskAddress(ptr);
  uint64_t i;

  for (i = 1; i < count; ++i) {
    p += size;
    memcpy(&shadow_begin[p], &shadow_begin[p1], size);
  }
}

/**
 * Record the stored type and address in the shadow memory.
 */
void trackStoreInst(void *ptr, uint8_t typeNumber, uint64_t size, uint32_t tag) {
  uintptr_t p = maskAddress(ptr);
  shadow_begin[p] = typeNumber;
  memset(&shadow_begin[p + 1], 0, size - 1);
#if DEBUG
  printf("Store(%d): %p, %p = %u | %lu bytes | \n", tag, ptr, (void *)p, typeNumber, size);
#endif

}

/** 
 * Check that the two types match
 */
void compareTypes(uint8_t typeNumberSrc, uint8_t typeNumberDest, uint32_t tag) {
  if(typeNumberSrc != typeNumberDest) {
    printf("Type mismatch(%u): expecting %s, found %s! \n", tag, typeNames[typeNumberDest], typeNames[typeNumberSrc]);
  }
}

/**
 * Check that number of VAArg accessed is not greater than those passed
 */
void compareNumber(uint64_t NumArgsPassed, uint64_t ArgAccessed, uint32_t tag){
  if(ArgAccessed > NumArgsPassed) {
    printf("Type mismatch(%u): Accessing variable %lu, passed only %lu! \n", tag, ArgAccessed, NumArgsPassed);
  }
}

/**
 * Combined check for Va_arg. 
 * Check that no. of arguments is less than passed
 * Check that the type being accessed is correct
 * MD : pointer to array of metadata for each argument passed
 */
void compareTypeAndNumber(uint64_t NumArgsPassed, uint64_t ArgAccessed, uint8_t TypeAccessed, void *MD, uint32_t tag) {
  compareNumber(NumArgsPassed, ArgAccessed, tag);
  compareTypes(TypeAccessed, ((uint8_t*)MD)[ArgAccessed], tag);
}

/**
 * Check the loaded type against the type recorded in the shadow memory.
 */
void trackLoadInst(void *ptr, uint8_t typeNumber, uint64_t size, uint32_t tag) {
  uint8_t i = 1;
  uintptr_t p = maskAddress(ptr);
  assert(p + size < SIZE);

#if DEBUG
  printf("Load(%d): %p, %p = actual: %u, expect: %u | %lu  bytes\n", tag, ptr, (void *)p, typeNumber, shadow_begin[p], size);
#endif

  /* Check if this an initialized but untyped memory.*/
  if (typeNumber != shadow_begin[p]) {
    if (shadow_begin[p] != 0xFF) {
      printf("Type mismatch(%u): %p expecting %s, found %s!\n", tag, ptr, typeNames[typeNumber], typeNames[shadow_begin[p]]);
      return;
    } else {
      /* If so, set type to the type being read.
         Check that none of the bytes are typed.*/
      for (; i < size; ++i) {
        if (0xFF != shadow_begin[p + i]) {
          printf("Type alignment mismatch(%u): expecting %s, found %s!\n", tag, typeNames[typeNumber], typeNames[shadow_begin[p+i]]);
          break;
        }
      }
      trackStoreInst(ptr, typeNumber, size, tag);
      return ;
    }
  }

  for (; i < size; ++i) {
    if (0 != shadow_begin[p + i]) {
      printf("Type alignment mismatch(%u): expecting %s, found %s!\n", tag, typeNames[typeNumber], typeNames[shadow_begin[p]]);
      break;
    }
  }
}

/**
 *  For memset type instructions, that set values. 
 *  0xFF type indicates that any type can be read, 
 */
void trackInitInst(void *ptr, uint64_t size, uint32_t tag) {
  uintptr_t p = maskAddress(ptr);
  memset(&shadow_begin[p], 0xFF, size);
#if DEBUG
  printf("Initialize: %p, %p | %lu bytes | %u\n", ptr, (void *)p, size, tag);
#endif
}

/**
 * Clear the metadata for given pointer
 */
void trackUnInitInst(void *ptr, uint64_t size, uint32_t tag) {
  uintptr_t p = maskAddress(ptr);
  memset(&shadow_begin[p], 0x00, size);
#if DEBUG
  printf("Unitialize: %p, %p | %lu bytes | %u\n", ptr, (void *)p, size, tag);
#endif
}

/**
 * Copy size bits of metadata from src ptr to dest ptr.
 */
void copyTypeInfo(void *dstptr, void *srcptr, uint64_t size, uint32_t tag) {
  uintptr_t d = maskAddress(dstptr);
  uintptr_t s = maskAddress(srcptr);
  memcpy(&shadow_begin[d], &shadow_begin[s], size);
#if DEBUG
  printf("Copy(%d): %p, %p = %u | %lu bytes \n", tag, dstptr, srcptr, shadow_begin[s], size);
#endif
}

/**
 * Initialize metadata for the pointer returned by __ctype_b_loc
 */
void trackctype(void *ptr, uint32_t tag) {
  short *p, *p1;
  trackInitInst(ptr, sizeof(short*), tag);
  p = *(short**)ptr;
  p1 = p -128;
  trackInitInst(p1, sizeof(short)*384, tag);
}

void trackctype_32(void *ptr, uint32_t tag) {
  int *p, *p1;
  trackInitInst(ptr, sizeof(int*), tag);
  p = *(int**)ptr;
  p1 = p -128 ;
  trackInitInst(p1, sizeof(int)*384, tag);
}

/**
 * Initialize metadata for the dst pointer of strncpy
 */
void trackStrncpyInst(void *dst, void *src, uint64_t size, uint32_t tag) {
  if(strlen(src) < size)
    size = strlen(src) + 1;
  copyTypeInfo(dst, src, size, tag);
}

/**
 * Initialize metadata for the dst pointer of strcpy
 */
void trackStrcpyInst(void *dst, void *src, uint32_t tag) {
  copyTypeInfo(dst, src, strlen(src)+1, tag);
}

void trackStrcatInst(void *dst, void *src, uint32_t tag) {
  uintptr_t dst_start = (uintptr_t)(dst) + strlen(dst) -1;
  copyTypeInfo((void*)dst_start, src, strlen(src)+1, tag);
}

void trackgetcwd(void *ptr, uint32_t tag) {
  trackInitInst(ptr, strlen(ptr) + 1, tag);
}

void trackgethostname(void *ptr, uint32_t tag) {
  trackInitInst(ptr, strlen(ptr) + 1, tag);
}
