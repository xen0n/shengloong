#ifndef _shengloong_processing_isa_ext_h
#define _shengloong_processing_isa_ext_h

#include <libelf.h>

#include "ctx.h"

// ISA extension types
typedef enum {
    ISA_EXT_NONE = 0,
    ISA_EXT_LSX = 1 << 0,   // 128-bit SIMD
    ISA_EXT_LASX = 1 << 1,  // 256-bit SIMD
    ISA_EXT_LBT = 1 << 2,   // Binary Translation
    ISA_EXT_LVZ = 1 << 3,   // Virtualization
} isa_ext_flags_t;

void scan_for_isa_extensions(struct sl_elf_ctx *ctx, Elf_Scn *s);
void isa_ext_print_final_report(void);

#endif  // _shengloong_processing_isa_ext_h
