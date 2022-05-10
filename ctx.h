#ifndef _shengloong_ctx_h
#define _shengloong_ctx_h

#include <stdbool.h>

#include <libelf.h>

struct sl_elf_ctx {
	const struct sl_cfg *cfg;

	const char *path;
	Elf *e;

	size_t dynstr;
	Elf_Data *dynstr_d;

	bool dirty;
};

const char *sl_elf_dynstr(const struct sl_elf_ctx *ctx, size_t idx);
const char *sl_elf_raw_dynstr(const struct sl_elf_ctx *ctx, size_t off);
int sl_elf_patch_dynstr_by_off(struct sl_elf_ctx *ctx, size_t off, const char *newval);
int sl_elf_patch_dynstr_by_idx(struct sl_elf_ctx *ctx, size_t idx, const char *newval);

#endif  // _shengloong_ctx_h
