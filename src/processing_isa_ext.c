#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "gettext.h"
#include "isa_ext_classifier.gen.h"
#include "processing_isa_ext.h"

#define _(x) gettext(x)

// Global structure to track ISA extension usage per file
struct isa_ext_usage {
    const char *path;
    isa_ext_flags_t extensions;
    struct isa_ext_usage *next;
};

static struct isa_ext_usage *g_isa_ext_list = NULL;
static bool g_has_isa_ext_usage = false;

/////////////////////////////////////////////////////////////////////////////

// Note: Instruction classifiers (is_lsx_insn, is_lasx_insn, is_lbt_insn, is_lvz_insn)
// are now auto-generated from loongarch-opcodes data and included via isa_ext_classifier.gen.h

#define READ_INSN(x) le32toh(*x)

void scan_for_isa_extensions(struct sl_elf_ctx *ctx, Elf_Scn *s)
{
    isa_ext_flags_t extensions = ISA_EXT_NONE;
    
    Elf_Data *d = NULL;
    while ((d = elf_getdata(s, d)) != NULL) {
        // Check for null buffer
        if (d->d_buf == NULL || d->d_size == 0) {
            continue;
        }
        
        uint32_t *p = d->d_buf;
        // Align down to 4-byte boundary - .text sections should be properly aligned,
        // but we ensure we don't read past the end if size is not a multiple of 4.
        // Partial instructions at the end can be safely ignored.
        uint32_t *end = (uint32_t *)((uint8_t *)d->d_buf + (d->d_size & ~3));
        
        for (; p < end; p++) {
            uint32_t insn = READ_INSN(p);
            
            // Check each extension type
            if (is_lsx_insn(insn)) {
                extensions |= ISA_EXT_LSX;
            }
            
            if (is_lasx_insn(insn)) {
                extensions |= ISA_EXT_LASX;
            }
            
            if (is_lbt_insn(insn)) {
                extensions |= ISA_EXT_LBT;
            }
            
            if (is_lvz_insn(insn)) {
                extensions |= ISA_EXT_LVZ;
            }
            
            // Early exit if all extensions found
            if (extensions == (ISA_EXT_LSX | ISA_EXT_LASX | ISA_EXT_LBT | ISA_EXT_LVZ)) {
                break;
            }
        }
        
        if (extensions == (ISA_EXT_LSX | ISA_EXT_LASX | ISA_EXT_LBT | ISA_EXT_LVZ)) {
            break;
        }
    }
    
    // If any extensions found, record them
    if (extensions != ISA_EXT_NONE) {
        g_has_isa_ext_usage = true;
        
        // Check if this file is already in the list
        struct isa_ext_usage *existing = g_isa_ext_list;
        while (existing != NULL) {
            if (strcmp(existing->path, ctx->path) == 0) {
                // Update existing entry
                existing->extensions |= extensions;
                return;
            }
            existing = existing->next;
        }
        
        // Add new entry
        struct isa_ext_usage *new_entry = malloc(sizeof(struct isa_ext_usage));
        if (new_entry == NULL) {
            return;
        }
        
        new_entry->path = strdup(ctx->path);
        if (new_entry->path == NULL) {
            free(new_entry);
            return;
        }
        
        new_entry->extensions = extensions;
        new_entry->next = g_isa_ext_list;
        g_isa_ext_list = new_entry;
    }
}

void isa_ext_print_final_report(void)
{
    if (!g_has_isa_ext_usage) {
        printf(_(
            "\n\x1b[32m * \x1b[mNo ISA extension usage was found on your system!\n\n"
        ));
        return;
    }
    
    printf(_("\n\x1b[1;33m# ISA Extension Usage Report\x1b[m\n\n"));
    
    // Print summary by extension type
    int lsx_count = 0, lasx_count = 0, lbt_count = 0, lvz_count = 0;
    
    struct isa_ext_usage *entry = g_isa_ext_list;
    while (entry != NULL) {
        if (entry->extensions & ISA_EXT_LSX) lsx_count++;
        if (entry->extensions & ISA_EXT_LASX) lasx_count++;
        if (entry->extensions & ISA_EXT_LBT) lbt_count++;
        if (entry->extensions & ISA_EXT_LVZ) lvz_count++;
        entry = entry->next;
    }
    
    printf(_("Extension usage summary:\n"));
    if (lsx_count > 0) {
        printf(_("  \x1b[36mLSX\x1b[m  (128-bit SIMD): %d file(s)\n"), lsx_count);
    }
    if (lasx_count > 0) {
        printf(_("  \x1b[36mLASX\x1b[m (256-bit SIMD): %d file(s)\n"), lasx_count);
    }
    if (lbt_count > 0) {
        printf(_("  \x1b[36mLBT\x1b[m  (Binary Translation): %d file(s)\n"), lbt_count);
    }
    if (lvz_count > 0) {
        printf(_("  \x1b[36mLVZ\x1b[m  (Virtualization): %d file(s)\n"), lvz_count);
    }
    
    printf(_("\nDetailed file list:\n"));
    
    entry = g_isa_ext_list;
    while (entry != NULL) {
        printf("  %s:", entry->path);
        
        bool first = true;
        if (entry->extensions & ISA_EXT_LSX) {
            printf(" LSX");
            first = false;
        }
        if (entry->extensions & ISA_EXT_LASX) {
            printf("%sLASX", first ? " " : ", ");
            first = false;
        }
        if (entry->extensions & ISA_EXT_LBT) {
            printf("%sLBT", first ? " " : ", ");
            first = false;
        }
        if (entry->extensions & ISA_EXT_LVZ) {
            printf("%sLVZ", first ? " " : ", ");
        }
        printf("\n");
        
        entry = entry->next;
    }
    
    printf(_(
        "\n"
        "\x1b[33m * \x1b[mNote: These files use ISA extensions that may not be available\n"
        "   on all LoongArch hardware. Ensure your CPU supports these extensions.\n"
        "\n"
    ));
    
    // Cleanup
    while (g_isa_ext_list != NULL) {
        struct isa_ext_usage *next = g_isa_ext_list->next;
        free((void *)g_isa_ext_list->path);
        free(g_isa_ext_list);
        g_isa_ext_list = next;
    }
}
