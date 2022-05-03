#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <elf.h>
#include <gelf.h>
#include <libelf.h>

#ifndef EM_LOONGARCH
#define EM_LOONGARCH 258
#endif

static uint_fast32_t
dl_new_hash (const char *s)
{
	uint_fast32_t h = 5381;
	for (unsigned char c = *s; c != '\0'; c = *++s)
		h = h * 33 + c;
	return h & 0xffffffff;
}

/////////////////////////////////////////////////////////////////////////////

struct sl_cfg {
	int verbose;
	bool dry_run;

	const char *to_ver;
	Elf64_Word to_vna_hash;
};

static bool sl_cfg_is_ver_interesting(const struct sl_cfg *cfg, const char *ver)
{
	// we're only interested in symbol versions like "GLIBC_2.xx"
	return (strncmp("GLIBC_2.", ver, 8) == 0) && (strcmp(cfg->to_ver, ver) != 0);
}

/////////////////////////////////////////////////////////////////////////////

struct sl_elf_ctx {
	const struct sl_cfg *cfg;

	const char *path;
	Elf *e;

	size_t dynstr;
	Elf_Data *dynstr_d;

	bool dirty;
};

static const char *sl_elf_dynstr(const struct sl_elf_ctx *ctx, size_t idx)
{
	return elf_strptr(ctx->e, ctx->dynstr, idx);
}

static const char *sl_elf_raw_dynstr(const struct sl_elf_ctx *ctx, size_t off)
{
	return (const char *)(ctx->dynstr_d->d_buf + off);
}

static int sl_elf_patch_dynstr_by_off(struct sl_elf_ctx *ctx, size_t off, const char *newval)
{
	char *oldval = ctx->dynstr_d->d_buf + off;
	size_t oldlen = strlen(oldval);
	size_t newlen = strlen(newval);

	// we cannot alter string's length at present, but this is not a problem
	// as all strings we're interested in are like "GLIBC_2.xx"
	if (oldlen != newlen) {
		fprintf(
			stderr,
			"%s: cannot patch string with unequal lengths: attempted '%s' -> '%s'\n",
			ctx->path,
			oldval,
			newval
		);
		return EX_DATAERR;
	}

	ctx->dirty = true;
	memmove(oldval, newval, newlen);
	elf_flagdata(ctx->dynstr_d, ELF_C_SET, ELF_F_DIRTY);

	return 0;
}

static int sl_elf_patch_dynstr_by_idx(struct sl_elf_ctx *ctx, size_t idx, const char *newval)
{
	// get the idx-th string's offset
	const char *s = sl_elf_dynstr(ctx, idx);
	size_t off = (size_t)((void *)s - ctx->dynstr_d->d_buf);
	return sl_elf_patch_dynstr_by_off(ctx, off, newval);
}

/////////////////////////////////////////////////////////////////////////////

static int process(const struct sl_cfg *cfg, const char *path);
static int process_elf(struct sl_elf_ctx *ctx);
static int process_elf_dynsym(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);
static int process_elf_gnu_version_r(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);

static int process(const struct sl_cfg *cfg, const char *path)
{
	int fd;
	Elf *e;
	int ret = 0;

	fd = open(path, cfg->dry_run ? O_RDONLY : O_RDWR, 0);
	if (fd < 0) {
		return EX_NOINPUT;
	}

	e = elf_begin(fd, cfg->dry_run ? ELF_C_READ_MMAP : ELF_C_RDWR_MMAP, NULL);
	if (!e) {
		fprintf(stderr, "elf_begin on %s (fd %d) failed: %s\n", path, fd, elf_errmsg(-1));
		goto close;
	}

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

	default:
		// not handled
		// specifically, ar(1) archives don't need patching, as the expected
		// usage of this program is to patch sysroot then re-emerge world,
		// so static libraries get properly rebuilt
		break;
	}

	(void) elf_end(e);
	(void) close(fd);

	return ret;

close:
	(void) close(fd);
	return EX_SOFTWARE;
}

static int process_elf(struct sl_elf_ctx *ctx)
{
	Elf *e = ctx->e;

	size_t len;
	const char *ident = elf_getident(e, &len);
	if (len != EI_NIDENT) {
		return EX_SOFTWARE;
	}

	// only process ELF64 files for now
	if (ident[EI_CLASS] != ELFCLASS64) {
		return 0;
	}

	Elf64_Ehdr *ehdr = elf64_getehdr(e);

	// only process LoongArch files
	if (ehdr->e_machine != EM_LOONGARCH) {
		return 0;
	}

	size_t shstrndx;
	if (elf_getshdrstrndx(e, &shstrndx) != 0) {
		return EX_SOFTWARE;
	}

	Elf_Scn *s_dynsym = NULL;
	size_t nr_dynsym = 0;
	Elf_Scn *s_gnu_version_r = NULL;
	size_t nr_gnu_version_r = 0;
	{
		Elf_Scn *scn = NULL;
		size_t i = 0;  // section idx, 0 is naturally skipped
		while ((scn = elf_nextscn(e, scn)) != NULL) {
			i++;

			GElf_Shdr shdr;
			if (gelf_getshdr(scn, &shdr) != &shdr) {
				return EX_SOFTWARE;
			}

			const char *scn_name;
			if ((scn_name = elf_strptr(e, shstrndx, shdr.sh_name)) == NULL) {
				return EX_SOFTWARE;
			}

			if (!strncmp(".dynstr", scn_name, 7)) {
				ctx->dynstr = i;
				ctx->dynstr_d = elf_getdata(scn, NULL);
				continue;
			}
			if (!strncmp(".dynsym", scn_name, 7)) {
				s_dynsym = scn;
				nr_dynsym = shdr.sh_size;
				continue;
			}
			// we don't really need to check .gnu.version, because the
			// versions referred to all come from here
			if (!strncmp(".gnu.version_r", scn_name, 14)) {
				s_gnu_version_r = scn;
				nr_gnu_version_r = shdr.sh_info;
				continue;
			}
		}
	}

	if (s_dynsym) {
		int ret = process_elf_dynsym(ctx, s_dynsym, nr_dynsym);
		if (ret) {
			return ret;
		}
	}

	if (s_gnu_version_r) {
		int ret = process_elf_gnu_version_r(ctx, s_gnu_version_r, nr_gnu_version_r);
		if (ret) {
			return ret;
		}
	}

	if (!ctx->cfg->dry_run && ctx->dirty) {
		// don't alter preexisting layout
		elf_flagelf(e, ELF_C_SET, ELF_F_LAYOUT);
		if (elf_update(e, ELF_C_WRITE_MMAP) < 0) {
			fprintf(stderr, "%s: elf_update failed: %s\n", ctx->path, elf_errmsg(-1));
			return EX_SOFTWARE;
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
		while ((void *)sym < (d->d_buf + d->d_size)) {
			if (ELF64_ST_TYPE(sym->st_info) != STT_OBJECT) {
				// STT_FUNC names are stored without version information,
				// so only look at STT_OBJECT symbols
				goto next_sym;
			}

			// it seems version symbols are all stored with STN_ABS
			if (sym->st_shndx != SHN_ABS) {
				goto next_sym;
			}

			const char *ver_name = sl_elf_dynstr(ctx, sym->st_name);
			if (ctx->cfg->verbose >= 2) {
				printf("%s: announced symbol version %s at idx %zd\n", ctx->path, ver_name, i);
			}

			if (!sl_cfg_is_ver_interesting(ctx->cfg, ver_name)) {
				goto next_sym;
			}

			if (ctx->cfg->dry_run) {
				printf("%s: symbol version %s at idx %zd needs patching\n", ctx->path, ver_name, i);
				goto next_sym;
			}

			printf("%s: patching symbol version %s at idx %zd -> %s\n", ctx->path, ver_name, i, ctx->cfg->to_ver);
			int ret = sl_elf_patch_dynstr_by_idx(ctx, sym->st_name, ctx->cfg->to_ver);
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
			if (ctx->cfg->verbose >= 2) {
				const char *dep_filename = sl_elf_raw_dynstr(ctx, vn->vn_file);
				printf("%s: verneed %zd: depending on %s\n", ctx->path, i, dep_filename);
			}

			size_t j = 0;
			Elf64_Vernaux *aux = (Elf64_Vernaux *)((void *)vn + vn->vn_aux);
			for (j = 0; j < vn->vn_cnt; j++, aux = (Elf64_Vernaux *)((void *)aux + aux->vna_next)) {
				const char *vna_name_str = sl_elf_raw_dynstr(ctx, aux->vna_name);
				if (ctx->cfg->verbose >= 2) {
					printf("%s: verneed %zd: aux %zd name %s\n", ctx->path, i, j, vna_name_str);
				}

				if (!sl_cfg_is_ver_interesting(ctx->cfg, vna_name_str)) {
					continue;
				}

				if (ctx->cfg->dry_run) {
					printf(
						"%s: verneed %zd: aux %zd name %s needs patching\n",
						ctx->path,
						i,
						j,
						vna_name_str
					);
					continue;
				}

				printf(
					"%s: patching verneed %zd aux %zd %s -> %s\n",
					ctx->path,
					i,
					j,
					vna_name_str,
					ctx->cfg->to_ver
				);

				// patch dynstr
				int ret = sl_elf_patch_dynstr_by_off(ctx, aux->vna_name, ctx->cfg->to_ver);
				if (ret) {
					return ret;
				}

				// patch hash
				aux->vna_hash = ctx->cfg->to_vna_hash;
				elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
			}

			i++;
			vn = (Elf64_Verneed *)((void *)vn + vn->vn_next);
		}
	}

	return 0;
}

int main(int argc, const char *argv[])
{
	int i;
	int ret;

	if (argc < 2) {
		fprintf(
			stderr,
			"usage: %s <list of elf files to patch>\n",
			(argc > 0 && argv[0]) ? argv[0] : "shengloong"
		);
		return EX_USAGE;
	}

	const char *new_ver = "GLIBC_2.36";
	struct sl_cfg cfg = {
		.verbose = 1,
		.dry_run = false,

		.to_ver = new_ver,
		.to_vna_hash = dl_new_hash(new_ver),
	};

	if (elf_version(EV_CURRENT) == EV_NONE) {
		errx(EX_SOFTWARE, "libelf initialization failed: %s", elf_errmsg(-1));
	}

	for (i = 1; i < argc; i++) {
		// TODO: non-dry-run
		ret = process(&cfg, argv[i]);
		if (ret) {
			return ret;
		}
	}

	return 0;
}
