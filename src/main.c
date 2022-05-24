#include <err.h>
#include <fcntl.h>
#include <ftw.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <elf.h>
#include <gelf.h>
#include <libelf.h>
#include <popt.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "ctx.h"
#include "elfcompat.h"
#include "gettext.h"
#include "processing_syscall_abi.h"
#include "utils.h"
#include "walkdir.h"

#define _(x) gettext(x)
#define PO_PACKAGE_NAME "shengloong"

struct sl_cfg global_cfg;

/////////////////////////////////////////////////////////////////////////////

static void
__attribute__((noreturn))
usage(poptContext pctx, const char *error)
{
    poptPrintUsage(pctx, stderr, 0);
    if (error) {
        fprintf(stderr, "%s\n", error);
    }
    exit(EX_USAGE);
}

#define DEFAULT_FROM "GLIBC_2.35"
#define DEFAULT_TO "GLIBC_2.36"

int main(int argc, const char *argv[])
{
#if defined(ENABLE_NLS) && ENABLE_NLS
    setlocale(LC_ALL, "");
    bindtextdomain(PO_PACKAGE_NAME, CONFIG_LOCALEDIR);
    textdomain(PO_PACKAGE_NAME);
#endif

    struct sl_cfg cfg = {
        .verbose = false,
        .dry_run = false,

        .from_ver = DEFAULT_FROM,
        .to_ver = DEFAULT_TO,
    };

    struct poptOption options[] = {
        { "verbose", 'v', POPT_ARG_NONE, &cfg.verbose, 0, _("produce more (debugging) output"), NULL },
        { "pretend", 'p', POPT_ARG_NONE, &cfg.dry_run, 0, _("don't actually patch the files"), NULL },
        { "from-ver", 'f', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &cfg.from_ver, 0, _("migrate from this glibc symbol version"), "GLIBC_2.3x" },
        { "to-ver", 't', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &cfg.to_ver, 0, _("migrate to this glibc symbol version"), "GLIBC_2.3y" },
        { "check-syscall-abi", 'a', POPT_ARG_NONE, &cfg.check_syscall_abi, 0, _("scan for syscall ABI incompatibility, don't patch files"), NULL },
        POPT_AUTOHELP
        POPT_TABLEEND
    };

    poptContext pctx = poptGetContext(NULL, argc, argv, options, 0);
    poptSetOtherOptionHelp(pctx, _("<root dirs>"));
    if (argc < 2) {
        usage(pctx, NULL);
    }

    int ret = poptGetNextOpt(pctx);
    if (ret < -1) {
        fprintf(
            stderr,
            "%s: %s\n",
            poptBadOption(pctx, POPT_BADOPTION_NOALIAS),
            poptStrerror(ret)
        );
        exit(EX_USAGE);
    }

    if (poptPeekArg(pctx) == NULL) {
        usage(pctx, _("at least one directory argument is required"));
    }

    cfg.from_elfhash = bfd_elf_hash(cfg.from_ver);
    cfg.to_elfhash = bfd_elf_hash(cfg.to_ver);

    if (cfg.check_syscall_abi) {
        cfg.dry_run = 1;
    }

    global_cfg = cfg;

    // GCOVR_EXCL_START: impossible to fail before ELF v2 is released which is extremely unlikely
    if (elf_version(EV_CURRENT) == EV_NONE) {
        errx(EX_SOFTWARE, _("libelf initialization failed: %s"), elf_errmsg(-1));
    }
    // GCOVR_EXCL_STOP

    const char *dir;
    while ((dir = poptGetArg(pctx)) != NULL) {
        ret = process_dir(dir);
        if (ret) {
            return ret;
        }
    }

    if (cfg.check_syscall_abi) {
        print_final_report();
    }


    poptFreeContext(pctx);

    return 0;
}
