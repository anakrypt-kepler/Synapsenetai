#pragma once
#include "synapse_msvc_prefix.h"
#include <cstddef>
#ifndef PROT_READ
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0
#define MAP_ANONYMOUS 0
#define MAP_FAILED (reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
inline void* mmap(void*, size_t, int, int, int, long) { return MAP_FAILED; }
inline int munmap(void*, size_t) { return -1; }
inline int madvise(void*, size_t, int) { return -1; }
#endif
