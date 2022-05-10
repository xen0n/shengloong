#ifndef _shengloong_processing_ldso_h
#define _shengloong_processing_ldso_h

#include <libelf.h>

#include "ctx.h"

int patch_ldso_rodata(struct sl_elf_ctx *ctx, Elf_Scn *s);
int patch_ldso_text_hashes(struct sl_elf_ctx *ctx, Elf_Scn *s);

#endif  // _shengloong_processing_ldso_h
