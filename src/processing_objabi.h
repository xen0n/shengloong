#ifndef _shengloong_processing_objabi_h
#define _shengloong_processing_objabi_h

#include <elf.h>

#include "ctx.h"

void check_objabi(struct sl_elf_ctx *ctx, Elf64_Word e_flags);
void objabi_print_final_report(void);

#endif  // _shengloong_processing_objabi_h
