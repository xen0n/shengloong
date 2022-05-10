#ifndef _shengloong_cfg_h
#define _shengloong_cfg_h

#include <stdbool.h>

#include <elf.h>

struct sl_cfg {
	int verbose;
	int dry_run;

	const char *from_ver;
	const char *to_ver;
	Elf64_Word from_elfhash;
	Elf64_Word to_elfhash;
};

bool sl_cfg_is_ver_interesting(const struct sl_cfg *cfg, const char *ver);

#endif  // _shengloong_cfg_h
