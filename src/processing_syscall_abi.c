#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <sys/param.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "gettext.h"
#include "processing_syscall_abi.h"

#define _(x) gettext(x)

static bool g_has_syscall_abi_problems = false;

/////////////////////////////////////////////////////////////////////////////

static bool is_syscall(uint32_t insn)
{
    // insn format is Ud15
    return (insn & 0xffff7000) == 0x002b0000;
}

// if insn is one of:
//
// - addi.[wd] $a7, $zero, <imm>
// - ori       $a7, $zero, <imm>
//
// then return imm; else return 0.
static uint32_t maybe_pull_out_syscall_nr(uint32_t insn)
{
    // DJSk12 and DJUk12 are the same in this case
    uint32_t opcode = insn & 0xffc00000;
    uint32_t rd, rj;
    switch (opcode) {
    case 0x02800000: // addi.w
    case 0x02c00000: // addi.d
    case 0x03800000: // ori
        rd = insn & 0x1f;
        rj = (insn >> 5) & 0x1f;
        if (rd != 11 || rj != 0) {
            return 0;
        }

        return (insn >> 10) & 0xfff;
    }

    return 0;
}

static bool is_clobbering_rd(uint32_t insn, int rd)
{
    // XXX not exact -- correct solution would require properly disassembling,
    // hence complete opcode info
    return (insn & 0x1f) == (uint32_t)rd;
}

#define READ_INSN(x) le32toh(*x)

// how many insns to look back for syscall number
#define MAX_REVERSE_SEARCH_WINDOW 20

void scan_for_removed_syscalls(struct sl_elf_ctx *ctx, Elf_Scn *s)
{
    Elf_Data *d = NULL;
    while ((d = elf_getdata(s, d)) != NULL) {
        uint32_t *p = d->d_buf;
        uint32_t *end = (uint32_t *)((uint8_t *)d->d_buf + d->d_size);
        size_t count = 0;

        for (; p < end; p++, count++) {
            uint32_t insn_word = READ_INSN(p);

            // find all syscall insns
            if (!is_syscall(insn_word)) {
                continue;
            }

            // we're looking at a syscall insn
            // now, reverse search for an immediate load into $a7 (the syscall
            // number)
            if (count == 0) {
                // Unlikely, but we're at the start of text section, and it's
                // a syscall.
                // This binary is very likely malformed, but it's unrelated to
                // our syscall ABI check, so just quietly ignore.
                continue;
            }

            uint32_t syscall_nr = 0;
            uint32_t *q = p - 1;
            size_t search_window = MIN(count - 1, MAX_REVERSE_SEARCH_WINDOW);
            for (; search_window > 0; q--, search_window--) {
                insn_word = READ_INSN(q);
                syscall_nr = maybe_pull_out_syscall_nr(insn_word);
                if (syscall_nr != 0) {
                    // Found it!
                    break;
                }

                if (is_clobbering_rd(insn_word, 11)) {
                    // stop looking; $a7 is being stuffed something we can't
                    // process.
                    break;
                }
            }

            if (syscall_nr == 0) {
                // we failed to statically pull out the syscall number
                continue;
            }

            const char *problematic_syscall = NULL;
            switch (syscall_nr) {
            case 79:
                problematic_syscall = "newfstatat";
                break;

            case 80:
                problematic_syscall = "fstat";
                break;

            case 163:
                problematic_syscall = "getrlimit";
                break;

            case 164:
                problematic_syscall = "setrlimit";
                break;
            }

            if (problematic_syscall == NULL) {
                // legitimate syscall
                continue;
            }

            g_has_syscall_abi_problems = true;
            printf(
                _("%s: usage of removed syscall `%s` at .text+0x%zx\n"),
                ctx->path,
                problematic_syscall,
                (uint8_t *)p - (uint8_t *)d->d_buf
            );
        }
    }
}

void print_final_report()
{
    if (g_has_syscall_abi_problems) {
        printf(_(
            "\n"
            "        \x1b[31m╔═══════════════════════════════════════════════════════════╗\x1b[m\n"
            "        \x1b[31m║                                                           ║\x1b[m\n"
            "        \x1b[31m║\x1b[m              You need to \x1b[1;31mUPGRADE YOUR LIBC\x1b[0;32m*\x1b[m,              \x1b[31m║\x1b[m\n"
            "        \x1b[31m║\x1b[m  \x1b[1;31mBEFORE\x1b[m you reboot into a kernel without these syscalls.  \x1b[31m║\x1b[m\n"
            "        \x1b[31m║                                                           ║\x1b[m\n"
            "        \x1b[31m╚═══════════════════════════════════════════════════════════╝\x1b[m\n"
            "\n"
            " \x1b[32m*\x1b[m If other non-libc programs are shown above, they should be rebuilt\n"
            "   after the libc upgrade as well.\n"
            "\n"
            "   You can run \x1b[32mshengloong -a\x1b[m again, after you have upgraded the libc,\n"
            "   if unsure.\n"
            "\n"
        ));
        return;
    }

    printf(_(
        "\x1b[32m\n"
        "        ╔═════════════════════════════════════════════════════════╗\n"
        "        ║                                                         ║\n"
        "        ║  \x1b[1mNo deprecated syscall usage was found on your system!\x1b[0;32m  ║\n"
        "        ║                                                         ║\n"
        "        ╚═════════════════════════════════════════════════════════╝\n"
        "\x1b[m\n"
    ));
}
