#ifndef _shengloong_processing_abi_features_h
#define _shengloong_processing_abi_features_h

#include <elf.h>
#include <libelf.h>

#include "ctx.h"

// ABI feature types
typedef enum {
    ABI_FEATURE_NONE = 0,
    ABI_FEATURE_RELR = 1 << 0,      // DT_RELR support
    ABI_FEATURE_TLSDESC = 1 << 1,   // TLSDESC support
} abi_feature_flags_t;

void check_abi_features(struct sl_elf_ctx *ctx, Elf *e);
void abi_features_print_final_report(void);

#endif  // _shengloong_processing_abi_features_h
