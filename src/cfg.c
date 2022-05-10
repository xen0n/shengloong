#include <string.h>

#include "cfg.h"

bool sl_cfg_is_ver_interesting(
	const struct sl_cfg *cfg __attribute__((unused)),
	const char *ver)
{
	// we're only interested in symbol versions like "GLIBC_2.3x"
	return strncmp("GLIBC_2.3", ver, 9) == 0;
}
