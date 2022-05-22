#ifndef _shengloong_processing_syscall_abi_h
#define _shengloong_processing_syscall_abi_h

#include <libelf.h>

#include "ctx.h"

void scan_for_fstatxx(struct sl_elf_ctx *ctx, Elf_Scn *s);
void print_final_report(void);

#endif  // _shengloong_processing_syscall_abi_h
