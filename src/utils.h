#ifndef _shengloong_utils_h
#define _shengloong_utils_h

#include <stdbool.h>
#include <stddef.h>

unsigned long bfd_elf_hash (const char *namearg);
bool endswith(const char *s, const char *pattern, size_t n);

#endif  // _shengloong_utils_h
