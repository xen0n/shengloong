#ifndef _shengloong_processing_symbol_versions_h
#define _shengloong_processing_symbol_versions_h

#include <libelf.h>

#include "ctx.h"

void collect_symbol_versions(struct sl_elf_ctx *ctx, Elf_Scn *scn, size_t n);
void symbol_versions_print_final_report(void);

#endif  // _shengloong_processing_symbol_versions_h
