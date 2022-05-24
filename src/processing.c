#include <endian.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <gelf.h>

#include "buildconfig.gen.h"
#include "elfcompat.h"
#include "gettext.h"
#include "processing.h"
#include "processing_ldso.h"
#include "processing_syscall_abi.h"
#include "utils.h"

#define _(x) gettext(x)

static int process_elf(struct sl_elf_ctx *ctx);
static int process_elf_dynsym(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);
static int process_elf_gnu_version_d(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);
static int process_elf_gnu_version_r(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);

// moves fd
int process(const struct sl_cfg *cfg, const char *path, int fd)
{
    Elf *e;
    int ret = 0;

    e = elf_begin(fd, cfg->dry_run ? ELF_C_READ_MMAP : ELF_C_RDWR_MMAP, NULL);
    // GCOVR_EXCL_START: excessively unlikely to happen
    if (!e) {
        fprintf(stderr, _("elf_begin on %s (fd %d) failed: %s\n"), path, fd, elf_errmsg(-1));
        goto close;
    }
    // GCOVR_EXCL_STOP

    struct sl_elf_ctx ctx = {
        .cfg = cfg,
        .path = path,
        .e = e,
        .dirty = false,
    };

    switch (elf_kind(e)) {
    case ELF_K_ELF:
        ret = process_elf(&ctx);
        break;

    // GCOVR_EXCL_START
    default:
        // currently impossible to reach, due to early checking of ELF magic
        // during directorywalking
        //
        // specifically, ar(1) archives don't need patching, as the expected
        // usage of this program is to patch sysroot then re-emerge world,
        // so static libraries get properly rebuilt
        __builtin_unreachable();
        // GCOVR_EXCL_STOP
    }

    (void) elf_end(e);
    (void) close(fd);

    return ret;

    // GCOVR_EXCL_START: excessively unlikely to happen
close:
    (void) close(fd);
    return EX_SOFTWARE;
    // GCOVR_EXCL_STOP
}

static int process_elf(struct sl_elf_ctx *ctx)
{
    Elf *e = ctx->e;

    size_t len;
    const char *ident = elf_getident(e, &len);
    // GCOVR_EXCL_START: virtually impossible
    if (len != EI_NIDENT) {
        return EX_SOFTWARE;
    }
    // GCOVR_EXCL_STOP

    // only process ELF64 files for now
    if (ident[EI_CLASS] != ELFCLASS64) {
        if (ctx->cfg->verbose) {
            printf(_("%s: ignoring: not ELF64 file\n"), ctx->path);
        }
        return 0;
    }

    // only process little-endian files for now
    if (ident[EI_DATA] != ELFDATA2LSB) {
        if (ctx->cfg->verbose) {
            printf(_("%s: ignoring: not little-endian\n"), ctx->path);
        }
        return 0;
    }

    Elf64_Ehdr *ehdr = elf64_getehdr(e);

    // only process LoongArch files
    if (ehdr->e_machine != EM_LOONGARCH) {
        if (ctx->cfg->verbose) {
            printf(
                _("%s: ignoring: not LoongArch file: e_machine = %d != %d\n"),
                ctx->path,
                ehdr->e_machine,
                EM_LOONGARCH
            );
        }
        return 0;
    }

    bool is_ldso = endswith(ctx->path, "ld-linux-loongarch-lp64d.so.1", 29);

    size_t shstrndx;
    // GCOVR_EXCL_START: excessively unlikely to happen
    if (elf_getshdrstrndx(e, &shstrndx) != 0) {
        // ignore malformed files -- every "normal" binary out there should
        // have named sections
        if (ctx->cfg->verbose) {
            printf(
                _("%s: ignoring: malformed file: no shstrndx\n"),
                ctx->path
            );
        }

        return 0;
    }
    // GCOVR_EXCL_STOP

    Elf_Scn *s_dynsym = NULL;
    size_t nr_dynsym = 0;
    Elf_Scn *s_gnu_version_d = NULL;
    size_t nr_gnu_version_d = 0;
    Elf_Scn *s_gnu_version_r = NULL;
    size_t nr_gnu_version_r = 0;
    Elf_Scn *s_rodata = NULL;
    Elf_Scn *s_text = NULL;
    {
        Elf_Scn *scn = NULL;
        size_t i = 0;  // section idx, 0 is naturally skipped
        while ((scn = elf_nextscn(e, scn)) != NULL) {
            i++;

            GElf_Shdr shdr;
            if (gelf_getshdr(scn, &shdr) != &shdr) {
                return EX_SOFTWARE;  // GCOVR_EXCL_LINE: virtually impossible
            }

            const char *scn_name;
            if ((scn_name = elf_strptr(e, shstrndx, shdr.sh_name)) == NULL) {
                // GCOVR_EXCL_START: virtually impossible
                if (ctx->cfg->verbose) {
                    printf(
                        _("%s: ignoring: malformed file: cannot get section name\n"),
                        ctx->path
                    );
                }

                return EX_SOFTWARE;
                // GCOVR_EXCL_STOP
            }

            if (is_ldso || ctx->cfg->check_syscall_abi) {
                if (!strcmp(".text", scn_name)) {
                    s_text = scn;

                    if (ctx->cfg->check_syscall_abi) {
                        // in syscall ABI check mode, only .text is needed
                        break;
                    }

                    continue;
                }
            }

            if (!strcmp(".dynstr", scn_name)) {
                ctx->dynstr = i;
                ctx->dynstr_d = elf_getdata(scn, NULL);
                continue;
            }
            if (!strcmp(".dynsym", scn_name)) {
                s_dynsym = scn;
                nr_dynsym = shdr.sh_size;
                continue;
            }
            // we don't really need to check .gnu.version, because the
            // versions referred to all come from here
            if (!strcmp(".gnu.version_d", scn_name)) {
                s_gnu_version_d = scn;
                nr_gnu_version_d = shdr.sh_info;
                continue;
            }
            if (!strcmp(".gnu.version_r", scn_name)) {
                s_gnu_version_r = scn;
                nr_gnu_version_r = shdr.sh_info;
                continue;
            }

            if (is_ldso) {
                if (!strcmp(".rodata", scn_name)) {
                    s_rodata = scn;
                    continue;
                }
            }
        }
    }

    if (ctx->cfg->check_syscall_abi) {
        if (s_text) {
            scan_for_fstatxx(ctx, s_text);
        }
        return 0;
    }

    if (s_gnu_version_d) {
        int ret = process_elf_gnu_version_d(ctx, s_gnu_version_d, nr_gnu_version_d);
        // GCOVR_EXCL_START: unlikely because no I/O is involved
        if (ret) {
            return ret;
        }
        // GCOVR_EXCL_STOP
    }

    if (s_gnu_version_r) {
        int ret = process_elf_gnu_version_r(ctx, s_gnu_version_r, nr_gnu_version_r);
        // GCOVR_EXCL_START: unlikely because no I/O is involved
        if (ret) {
            return ret;
        }
        // GCOVR_EXCL_STOP
    }

    if (s_dynsym) {
        int ret = process_elf_dynsym(ctx, s_dynsym, nr_dynsym);
        // GCOVR_EXCL_START: unlikely because no I/O is involved
        if (ret) {
            return ret;
        }
        // GCOVR_EXCL_STOP
    }

    if (is_ldso) {
        if (s_rodata) {
            int ret = patch_ldso_rodata(ctx, s_rodata);
            // GCOVR_EXCL_START: unlikely because no I/O is involved
            if (ret) {
                return ret;
            }
            // GCOVR_EXCL_STOP
        }

        if (s_text) {
            int ret = patch_ldso_text_hashes(ctx, s_text);
            // GCOVR_EXCL_START: unlikely because no I/O is involved
            if (ret) {
                return ret;
            }
            // GCOVR_EXCL_STOP
        }
    }

    if (!ctx->cfg->dry_run && ctx->dirty) {
        if (ctx->cfg->verbose) {
            printf(_("writing %s\n"), ctx->path);
        } else {
            printf(_("patching %s\n"), ctx->path);
        }

        // don't alter preexisting layout
        elf_flagelf(e, ELF_C_SET, ELF_F_LAYOUT);
        if (elf_update(e, ELF_C_WRITE_MMAP) < 0) {
            // GCOVR_EXCL_START: unlikely to happen except in cases like media error
            fprintf(stderr, _("%s: elf_update failed: %s\n"), ctx->path, elf_errmsg(-1));
            return EX_SOFTWARE;
            // GCOVR_EXCL_STOP
        }
    }

    return 0;
}

static int process_elf_dynsym(
    struct sl_elf_ctx *ctx,
    Elf_Scn *s,
    size_t n)
{
    size_t i = 0;
    Elf_Data *d = NULL;
    while (i < n && (d = elf_getdata(s, d)) != NULL) {
        Elf64_Sym *sym = (Elf64_Sym *)(d->d_buf);
        while ((void *)sym < (void *)((uint8_t *)d->d_buf + d->d_size)) {
            if (ELF64_ST_TYPE(sym->st_info) != STT_OBJECT) {
                // STT_FUNC names are stored without version information,
                // so only look at STT_OBJECT symbols
                goto next_sym;
            }

            // it seems version symbols are all stored with STN_ABS
            if (sym->st_shndx != SHN_ABS) {
                goto next_sym;
            }

            Elf64_Word st_name = sym->st_name;
            const char *ver_name = sl_elf_dynstr(ctx, st_name);

            if (!sl_cfg_is_ver_interesting(ctx->cfg, ver_name)) {
                goto next_sym;
            }

            if (ctx->cfg->dry_run) {
                printf(_("%s: symbol version %s at idx %zd needs patching\n"), ctx->path, ver_name, i);
                goto next_sym;
            }

            if (ctx->cfg->verbose) {
                printf(_("%s: patching symbol version %s at idx %zd -> %s\n"), ctx->path, ver_name, i, ctx->cfg->to_ver);
            }

            int ret = sl_elf_patch_dynstr_by_idx(ctx, st_name, ctx->cfg->to_ver);
            if (ret) {
                return ret;
            }

next_sym:
            i++;
            sym++;
        }
    }

    return 0;
}

static int process_elf_gnu_version_d(
    struct sl_elf_ctx *ctx,
    Elf_Scn *s,
    size_t n)
{
    size_t i = 0;
    Elf_Data *d = NULL;
    while (i < n && (d = elf_getdata(s, d)) != NULL) {
        Elf64_Verdef *vd = (Elf64_Verdef *)(d->d_buf);
        while (i < n) {
            Elf64_Verdaux *aux = (Elf64_Verdaux *)((uint8_t *)vd + vd->vd_aux);

            // only look at the first aux, because this aux is the vd's name
            Elf64_Word vda_name = aux->vda_name;
            const char *vda_name_str = sl_elf_dynstr(ctx, vda_name);

            if (!sl_cfg_is_ver_interesting(ctx->cfg, vda_name_str)) {
                goto next;
            }

            if (ctx->cfg->dry_run) {
                printf(
                    _("%s: verdef %zd: %s needs patching\n"),
                    ctx->path,
                    i,
                    vda_name_str
                );
                goto next;
            }

            if (ctx->cfg->verbose) {
                printf(_("%s: patching verdef %zd -> %s\n"), ctx->path, i, ctx->cfg->to_ver);
            }

            // patch dynstr
            int ret = sl_elf_patch_dynstr_by_idx(ctx, vda_name, ctx->cfg->to_ver);
            // GCOVR_EXCL_START: unlikely because no I/O is involved
            if (ret) {
                return ret;
            }
            // GCOVR_EXCL_STOP

            // patch hash
            if (vd->vd_hash != ctx->cfg->to_elfhash) {
                vd->vd_hash = ctx->cfg->to_elfhash;
                elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
                ctx->dirty = true;
            }

next:
            i++;
            vd = (Elf64_Verdef *)((uint8_t *)vd + vd->vd_next);
        }
    }

    return 0;
}

static int process_elf_gnu_version_r(
    struct sl_elf_ctx *ctx,
    Elf_Scn *s,
    size_t n)
{
    size_t i = 0;
    Elf_Data *d = NULL;
    while (i < n && (d = elf_getdata(s, d)) != NULL) {
        Elf64_Verneed *vn = (Elf64_Verneed *)(d->d_buf);
        while (i < n) {
            size_t j = 0;
            Elf64_Vernaux *aux = (Elf64_Vernaux *)((uint8_t *)vn + vn->vn_aux);
            for (j = 0; j < vn->vn_cnt; j++, aux = (Elf64_Vernaux *)((uint8_t *)aux + aux->vna_next)) {
                Elf64_Word vna_name = aux->vna_name;
                const char *vna_name_str = sl_elf_raw_dynstr(ctx, vna_name);

                if (!sl_cfg_is_ver_interesting(ctx->cfg, vna_name_str)) {
                    continue;
                }

                if (ctx->cfg->dry_run) {
                    printf(
                        _("%s: verneed %zd: aux %zd name %s needs patching\n"),
                        ctx->path,
                        i,
                        j,
                        vna_name_str
                    );
                    continue;
                }

                if (ctx->cfg->verbose) {
                    printf(
                        _("%s: patching verneed %zd aux %zd %s -> %s\n"),
                        ctx->path,
                        i,
                        j,
                        vna_name_str,
                        ctx->cfg->to_ver
                    );
                }

                // patch dynstr
                int ret = sl_elf_patch_dynstr_by_off(ctx, vna_name, ctx->cfg->to_ver);
                // GCOVR_EXCL_START: unlikely because no I/O is involved
                if (ret) {
                    return ret;
                }
                // GCOVR_EXCL_STOP

                // patch hash
                if (aux->vna_hash != ctx->cfg->to_elfhash) {
                    aux->vna_hash = ctx->cfg->to_elfhash;
                    elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
                    elf_flagscn(s, ELF_C_SET, ELF_F_DIRTY);
                    ctx->dirty = true;
                }
            }

            i++;
            vn = (Elf64_Verneed *)((uint8_t *)vn + vn->vn_next);
        }
    }

    return 0;
}
