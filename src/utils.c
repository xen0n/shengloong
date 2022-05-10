#include <string.h>

#include "utils.h"

unsigned long bfd_elf_hash (const char *namearg)
{
	const unsigned char *name = (const unsigned char *) namearg;
	unsigned long h = 0;
	unsigned long g;
	int ch;

	while ((ch = *name++) != '\0') {
		h = (h << 4) + ch;
		if ((g = (h & 0xf0000000)) != 0) {
			h ^= g >> 24;
			/* The ELF ABI says `h &= ~g', but this is equivalent in
			   this case and on some machines one insn instead of two.  */
			h ^= g;
		}
	}
	return h & 0xffffffff;
}


bool endswith(const char *s, const char *pattern, size_t n)
{
	size_t l = strlen(s);
	if (l < n) {
		return false;
	}

	s += l - n;
	return strncmp(s, pattern, n) == 0;
}
