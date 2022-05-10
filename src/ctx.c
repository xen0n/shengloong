#include <stdio.h>
#include <string.h>
#include <sysexits.h>

#include "ctx.h"

const char *sl_elf_dynstr(const struct sl_elf_ctx *ctx, size_t idx)
{
	return elf_strptr(ctx->e, ctx->dynstr, idx);
}

const char *sl_elf_raw_dynstr(const struct sl_elf_ctx *ctx, size_t off)
{
	return (const char *)((char *)ctx->dynstr_d->d_buf + off);
}

int sl_elf_patch_dynstr_by_off(struct sl_elf_ctx *ctx, size_t off, const char *newval)
{
	char *oldval = (char *)ctx->dynstr_d->d_buf + off;
	size_t oldlen = strlen(oldval);
	size_t newlen = strlen(newval);

	// we cannot alter string's length at present, but this is not a problem
	// as all strings we're interested in are like "GLIBC_2.xx"
	if (oldlen != newlen) {
		fprintf(
			stderr,
			"%s: cannot patch string with unequal lengths: attempted '%s' -> '%s'\n",
			ctx->path,
			oldval,
			newval
		);
		return EX_DATAERR;
	}

	// maintain idempotence to reduce work
	if (!strncmp(oldval, newval, newlen)) {
		return 0;
	}

	ctx->dirty = true;
	memmove(oldval, newval, newlen);
	elf_flagdata(ctx->dynstr_d, ELF_C_SET, ELF_F_DIRTY);

	return 0;
}

int sl_elf_patch_dynstr_by_idx(struct sl_elf_ctx *ctx, size_t idx, const char *newval)
{
	// get the idx-th string's offset
	const char *s = sl_elf_dynstr(ctx, idx);
	size_t off = (size_t)(s - (char *)ctx->dynstr_d->d_buf);
	return sl_elf_patch_dynstr_by_off(ctx, off, newval);
}
