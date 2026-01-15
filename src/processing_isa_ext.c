#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "gettext.h"
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

// Check if instruction is an LSX instruction (128-bit SIMD)
static bool is_lsx_insn(uint32_t insn)
{
    // LSX instructions primarily use these opcode ranges:
    // 0x7xxxxxxx range (0x70000000 - 0x73ffffff)
    // Also some in 0x09xxxxxx, 0x0cxxxxxx, 0x0dxxxxxx ranges
    // And memory ops in 0x2c000000, 0x30xxxxxx, 0x31xxxxxx, 0x38xxxxxx
    
    uint32_t op_high = insn & 0xff000000;
    
    // Main LSX instruction range
    if (op_high >= 0x70000000 && op_high <= 0x73000000) {
        return true;
    }
    
    // LSX floating-point multiply-add/sub (0x09xxxxxx range)
    if (op_high == 0x09000000) {
        return true;  // vfmadd, vfmsub, vfnmadd, vfnmsub
    }
    
    // LSX floating-point compare
    if ((insn & 0xffc00000) == 0x0c500000) {
        return true;  // vfcmp.*
    }
    
    // LSX bitsel and shuffle
    if ((insn & 0xff000000) == 0x0d000000) {
        uint32_t op_full = insn & 0xfff00000;
        if (op_full == 0x0d100000 || op_full == 0x0d500000) {
            return true;  // vbitsel.v, vshuf.b
        }
    }
    
    // LSX memory operations
    if ((insn & 0xffc00000) == 0x2c000000 || 
        (insn & 0xffc00000) == 0x2c400000) {
        return true;  // vld, vst
    }
    
    if ((insn & 0xff000000) == 0x30000000 || 
        (insn & 0xff000000) == 0x31000000) {
        return true;  // vldrepl.*, vstelm.*
    }
    
    if ((insn & 0xfff00000) == 0x38400000) {
        return true;  // vldx, vstx
    }
    
    return false;
}

// Check if instruction is a LASX instruction (256-bit SIMD)
static bool is_lasx_insn(uint32_t insn)
{
    // LASX instructions primarily use these opcode ranges:
    // 0x74xxxxxx - 0x77xxxxxx range
    // Also some in 0x0axxxxxx, 0x0cxxxxxx, 0x0dxxxxxx ranges
    // And memory ops in 0x2c800000, 0x32xxxxxx, 0x33xxxxxx, 0x38xxxxxx
    
    uint32_t op_high = insn & 0xff000000;
    
    // Main LASX instruction range
    if (op_high >= 0x74000000 && op_high <= 0x77000000) {
        return true;
    }
    
    // LASX floating-point multiply-add/sub
    if (op_high == 0x0a000000) {
        return true;  // xvfmadd, xvfmsub, xvfnmadd, xvfnmsub
    }
    
    // LASX floating-point compare
    if ((insn & 0xffc00000) == 0x0c900000 || 
        (insn & 0xffc00000) == 0x0ca00000) {
        return true;  // xvfcmp.*
    }
    
    // LASX bitsel and shuffle
    if ((insn & 0xff000000) == 0x0d000000) {
        uint32_t op_full = insn & 0xfff00000;
        if (op_full == 0x0d200000 || op_full == 0x0d600000) {
            return true;  // xvbitsel.v, xvshuf.b
        }
    }
    
    // LASX memory operations
    if ((insn & 0xffc00000) == 0x2c800000 || 
        (insn & 0xffc00000) == 0x2cc00000) {
        return true;  // xvld, xvst
    }
    
    if ((insn & 0xff000000) == 0x32000000 || 
        (insn & 0xff000000) == 0x33000000) {
        return true;  // xvldrepl.*, xvstelm.*
    }
    
    if ((insn & 0xfff00000) == 0x38480000) {
        return true;  // xvldx, xvstx
    }
    
    return false;
}

// Check if instruction is an LBT instruction (Binary Translation)
static bool is_lbt_insn(uint32_t insn)
{
    // LBT instructions use various opcode patterns:
    // 0x00000800 - 0x00000c00: movgr2scr, movscr2gr
    // 0x00007xxx: x86mttop, x86mftop, x86setloop*, x86inc/dec, x86settm, etc.
    // 0x001a0000, 0x001a8000: rotr.b, rotr.h
    // 0x00290000, 0x00298000: addu12i.w, addu12i.d
    // 0x003x0000 range: adc, sbc, rcr, armmove, x86setj, armsetj, arm*, x86*
    
    uint32_t op_full = insn & 0xfffff000;
    
    // movgr2scr, movscr2gr
    if (op_full == 0x00000800 || op_full == 0x00000c00) {
        return true;
    }
    
    // x86 top-of-stack and loop operations
    if ((insn & 0xffff0000) == 0x00007000) {
        return true;
    }
    
    // x86 inc/dec/settm/clrtm/inctop/dectop
    if ((insn & 0xfffff000) == 0x00008000) {
        return true;
    }
    
    // rotr.b, rotr.h
    if ((insn & 0xfff00000) == 0x001a0000) {
        return true;
    }
    
    // addu12i.w, addu12i.d
    if ((insn & 0xfff00000) == 0x00290000) {
        return true;
    }
    
    // adc, sbc, rcr, arm*, x86*
    if ((insn & 0xffc00000) == 0x00300000) {
        return true;
    }
    
    return false;
}

// Check if instruction is an LVZ instruction (Virtualization)
static bool is_lvz_insn(uint32_t insn)
{
    // LVZ instructions:
    // 0x05000000: gcsrxchg
    // 0x0648xxxx: gtlbclr, gtlbflush, gtlbsrch, gtlbrd, gtlbwr, gtlbfill
    // 0x002b8000: hypcall
    
    if ((insn & 0xff000000) == 0x05000000) {
        return true;  // gcsrxchg
    }
    
    if ((insn & 0xffff0000) == 0x06480000) {
        return true;  // gtlb* instructions
    }
    
    if ((insn & 0xffffff00) == 0x002b8000) {
        return true;  // hypcall
    }
    
    return false;
}

#define READ_INSN(x) le32toh(*x)

void scan_for_isa_extensions(struct sl_elf_ctx *ctx, Elf_Scn *s)
{
    isa_ext_flags_t extensions = ISA_EXT_NONE;
    
    Elf_Data *d = NULL;
    while ((d = elf_getdata(s, d)) != NULL) {
        uint32_t *p = d->d_buf;
        uint32_t *end = (uint32_t *)((uint8_t *)d->d_buf + d->d_size);
        
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
        if (new_entry != NULL) {
            new_entry->path = strdup(ctx->path);
            new_entry->extensions = extensions;
            new_entry->next = g_isa_ext_list;
            g_isa_ext_list = new_entry;
        }
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
