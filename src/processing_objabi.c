#include <stdbool.h>
#include <stdio.h>

#include <elf.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "elfcompat.h"
#include "gettext.h"
#include "processing_objabi.h"

#define _(x) gettext(x)

static bool g_has_objabi_problems = false;

/////////////////////////////////////////////////////////////////////////////

static bool is_objabi_okay(Elf64_Word ef)
{
    return (ef & EF_LARCH_OBJABI_MASK) >= EF_LARCH_OBJABI_V1;
}

void check_objabi(struct sl_elf_ctx *ctx, Elf64_Word e_flags)
{
    if (is_objabi_okay(e_flags)) {
        return;
    }

    g_has_objabi_problems = true;
    printf(
        _("%s: file uses obsolete object file ABI: e_flags=0x%x\n"),
        ctx->path,
        (int) e_flags
    );
}

void objabi_print_final_report()
{
    if (g_has_objabi_problems) {
        printf(_(
            "\n"
            "\x1b[31m * \x1b[mYour system has file(s) using obsolete object file ABI.\n"
            "   This may not play well with current or future toolchain components.\n"
            "\n"
            "   You may have to rebuild the affected packages or simply re-install your\n"
            "   system to fix this.\n"
            "\n"
        ));
        return;
    }

    printf(_(
        "\n\x1b[32m * \x1b[mNo obsolete object file ABI usage was found on your system!\n\n"
    ));
}
