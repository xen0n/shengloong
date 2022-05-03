#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <err.h>
#include <fcntl.h>
#include <ftw.h>
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

/////////////////////////////////////////////////////////////////////////////

static unsigned long bfd_elf_hash (const char *namearg)
{
	const unsigned char *name = (const unsigned char *) namearg;
	unsigned long h = 0;
	unsigned long g;
	int ch;

	while ((ch = *name++) != '\0') {
		h = (h << 4) + ch;
		if ((g = (h & 0xf0000000)) != 0) {
			h ^= g >> 24;
			/* The ELF ABI says `h &= ~g', but this is equivalent in
			   this case and on some machines one insn instead of two.  */
			h ^= g;
		}
	}
	return h & 0xffffffff;
}


static bool endswith(const char *s, const char *pattern, size_t n)
{
	size_t l = strlen(s);
	if (l < n) {
		return false;
	}

	s += l - n;
	return strncmp(s, pattern, n) == 0;
}

/////////////////////////////////////////////////////////////////////////////

struct sl_cfg {
	int verbose;
	bool dry_run;

	const char *from_ver;
	const char *to_ver;
	Elf64_Word from_elfhash;
	Elf64_Word to_elfhash;
};

static bool sl_cfg_is_ver_interesting(const struct sl_cfg *cfg, const char *ver)
{
	// we're only interested in symbol versions like "GLIBC_2.3x"
	return strncmp("GLIBC_2.3", ver, 9) == 0;
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

	// maintain idempotence to reduce work
	if (!strncmp(oldval, newval, newlen)) {
		return 0;
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

static int process(const struct sl_cfg *cfg, const char *path, int fd);
static int process_elf(struct sl_elf_ctx *ctx);
static int process_elf_dynsym(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);
static int process_elf_gnu_version_d(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);
static int process_elf_gnu_version_r(struct sl_elf_ctx *ctx, Elf_Scn *s, size_t n);
static int patch_ldso_rodata(struct sl_elf_ctx *ctx, Elf_Scn *s);
static int patch_ldso_text_hashes(struct sl_elf_ctx *ctx, Elf_Scn *s);

// moves fd
static int process(const struct sl_cfg *cfg, const char *path, int fd)
{
	Elf *e;
	int ret = 0;

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

	bool is_ldso = endswith(ctx->path, "ld-linux-loongarch-lp64d.so.1", 29);

	size_t shstrndx;
	if (elf_getshdrstrndx(e, &shstrndx) != 0) {
		return EX_SOFTWARE;
	}

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
			if (!strncmp(".gnu.version_d", scn_name, 14)) {
				s_gnu_version_d = scn;
				nr_gnu_version_d = shdr.sh_info;
				continue;
			}
			if (!strncmp(".gnu.version_r", scn_name, 14)) {
				s_gnu_version_r = scn;
				nr_gnu_version_r = shdr.sh_info;
				continue;
			}

			if (is_ldso) {
				if (!strncmp(".rodata", scn_name, 7)) {
					s_rodata = scn;
					continue;
				}
				if (!strncmp(".text", scn_name, 5)) {
					s_text = scn;
					continue;
				}
			}
		}
	}

	if (s_gnu_version_d) {
		int ret = process_elf_gnu_version_d(ctx, s_gnu_version_d, nr_gnu_version_d);
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

	if (s_dynsym) {
		int ret = process_elf_dynsym(ctx, s_dynsym, nr_dynsym);
		if (ret) {
			return ret;
		}
	}

	if (is_ldso) {
		if (s_rodata) {
			int ret = patch_ldso_rodata(ctx, s_rodata);
			if (ret) {
				return ret;
			}
		}

		if (s_text) {
			int ret = patch_ldso_text_hashes(ctx, s_text);
			if (ret) {
				return ret;
			}
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
			Elf64_Verdaux *aux = (Elf64_Verdaux *)((void *)vd + vd->vd_aux);

			// only look at the first aux, because this aux is the vd's name
			const char *vda_name_str = sl_elf_dynstr(ctx, aux->vda_name);
			if (ctx->cfg->verbose >= 2) {
				printf(
					"%s: verdef %zd: %s\n",
					ctx->path,
					i,
					vda_name_str
				);
			}

			if (!sl_cfg_is_ver_interesting(ctx->cfg, vda_name_str)) {
				goto next;
			}

			if (ctx->cfg->dry_run) {
				printf(
					"%s: verdef %zd: %s needs patching\n",
					ctx->path,
					i,
					vda_name_str
				);
				goto next;
			}

			printf("%s: patching verdef %zd -> %s\n", ctx->path, i, ctx->cfg->to_ver);

			// patch dynstr
			int ret = sl_elf_patch_dynstr_by_idx(ctx, aux->vda_name, ctx->cfg->to_ver);
			if (ret) {
				return ret;
			}

			// patch hash
			if (vd->vd_hash != ctx->cfg->to_elfhash) {
				vd->vd_hash = ctx->cfg->to_elfhash;
				elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
				ctx->dirty = true;
			}

next:
			i++;
			vd = (Elf64_Verdef *)((void *)vd + vd->vd_next);
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
				if (aux->vna_hash != ctx->cfg->to_elfhash) {
					aux->vna_hash = ctx->cfg->to_elfhash;
					elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
					elf_flagscn(s, ELF_C_SET, ELF_F_DIRTY);
					ctx->dirty = true;
				}
			}

			i++;
			vn = (Elf64_Verneed *)((void *)vn + vn->vn_next);
		}
	}

	return 0;
}

static int patch_ldso_rodata(struct sl_elf_ctx *ctx, Elf_Scn *s)
{
	Elf_Data *d = NULL;
	while ((d = elf_getdata(s, d)) != NULL) {
		void *curr = d->d_buf;
		size_t remaining = d->d_size;

		while (remaining > 0) {
			// search for "\0GLIBC_2.3x\0"
			// there should be only one reference
			void *p = memmem(curr, remaining, "\x00GLIBC_2.3", 10);
			if (p == NULL) {
				break;
			}

			remaining -= (p - curr) + 10;
			curr = p + 10;

			char *version_tag = p + 1;
			if (strlen(version_tag) != 10) {
				continue;
			}

			if (!strcmp(version_tag, ctx->cfg->to_ver)) {
				// idempotence
				continue;
			}

			if (ctx->cfg->dry_run) {
				printf(
					"%s: hard-coded symbol version in .rodata: %s (offset %zd) needs patching\n",
					ctx->path,
					version_tag,
					(void *)version_tag - d->d_buf
				);
				continue;
			}

			// patch
			printf(
				"%s: patching hard-coded symbol version in .rodata: %s (offset %zd) -> %s\n",
				ctx->path,
				version_tag,
				(void *)version_tag - d->d_buf,
				ctx->cfg->to_ver
			);
			memmove(version_tag, ctx->cfg->to_ver, 10);
			elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
			ctx->dirty = true;
		}
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////

static bool is_lu12i_w_with_imm(uint32_t insn, uint32_t imm)
{
	// insn format is DSj20 -- we match both opcode and imm part
	uint32_t match = 0x14000000 | ((imm & 0xfffff) << 5);
	return (insn & 0xffffffe0) == match;
}

static bool is_ori_exact(uint32_t insn, int rd, int rj, uint32_t imm)
{
	// insn format is DJUk12 -- we match exactly
	uint32_t match = 0x03800000 | ((imm & 0xfff) << 10) | (rj << 5) | rd;
	return insn == match;
}

static bool is_clobbering_rd(uint32_t insn, int rd)
{
	// XXX not exact -- correct solution would require properly disassembling,
	// hence complete opcode info
	return (insn & 0x1f) == rd;
}

static uint32_t patch_dsj20_imm(uint32_t old_insn, uint32_t new_imm)
{
	return (old_insn & 0xfe00001f) | ((new_imm & 0xfffff) << 5);
}

static uint32_t patch_djuk12_imm(uint32_t old_insn, uint32_t new_imm)
{
	return (old_insn & 0xffc003ff) | ((new_imm & 0xfff) << 10);
}

static int patch_ldso_text_hashes(struct sl_elf_ctx *ctx, Elf_Scn *s)
{
	uint32_t old_hash_lo12 = ctx->cfg->from_elfhash & 0xfff;
	uint32_t old_hash_hi20 = ctx->cfg->from_elfhash >> 12;

	uint32_t new_hash_lo12 = ctx->cfg->to_elfhash & 0xfff;
	uint32_t new_hash_hi20 = ctx->cfg->to_elfhash >> 12;

	Elf_Data *d = NULL;
	while ((d = elf_getdata(s, d)) != NULL) {
		uint32_t *p = d->d_buf;
		uint32_t *end = (uint32_t *)(d->d_buf + d->d_size);

		uint32_t *hi20_insn = NULL;
		int reg = 0;
		for (; p < end; p++) {
			if (hi20_insn == NULL) {
				// find first lu12i.w
				if (!is_lu12i_w_with_imm(*p, old_hash_hi20)) {
					continue;
				}

				hi20_insn = p;
				reg = (*p) & 0x1f;
				continue;
			}

			// find matching ori
			if (is_ori_exact(*p, reg, reg, old_hash_lo12)) {
				// found an immediate load of old hash
				if (ctx->cfg->dry_run) {
					printf(
						"%s: old hash in .text needs patching: lu12i.w offset %zd, ori offset %zd\n",
						ctx->path,
						(void *)hi20_insn - d->d_buf,
						(void *)p - d->d_buf
					);
					goto reset_state;
				}

				// patch
				uint32_t new_lu12i_w = patch_dsj20_imm(*hi20_insn, new_hash_hi20);
				uint32_t new_ori = patch_djuk12_imm(*p, new_hash_lo12);

				printf(
					"%s: patching old hash in .text: lu12i.w offset %zd %08x -> %08x, ori offset %zd %08x -> %08x\n",
					ctx->path,
					(void *)hi20_insn - d->d_buf,
					*hi20_insn,
					new_lu12i_w,
					(void *)p - d->d_buf,
					*p,
					new_ori
				);

				*hi20_insn = new_lu12i_w;
				*p = new_ori;
				elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);

				goto reset_state;
			}

			// if rd becomes clobbered, then restart matching lu12i.w,
			// otherwise keep searching for that ori
			if (is_clobbering_rd(*p, reg)) {
				goto reset_state;
			}

			continue;

reset_state:
			hi20_insn = NULL;
			reg = 0;
		}
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////

struct sl_cfg global_cfg;

static int walk_fn(
	const char *fpath,
	const struct stat *sb,
	int typeflag,
	struct FTW *ftwbuf)
{
	if (typeflag != FTW_F) {
		return FTW_CONTINUE;
	}

	if ((sb->st_mode & S_IFMT) != S_IFREG) {
		// we're only interested in regular files
		return FTW_CONTINUE;
	}

	if (sb->st_size < sizeof(Elf64_Ehdr)) {
		// ELF files must be at least this large
		return FTW_CONTINUE;
	}

	// check ELF magic bytes
	int fd = open(fpath, global_cfg.dry_run ? O_RDONLY : O_RDWR, 0);
	if (fd < 0) {
		// open failed, should not happen
		return FTW_STOP;
	}

	char magic[4];
	ssize_t nr_read = 0;
	while (nr_read < sizeof(magic)) {
		ssize_t n = read(fd, magic + nr_read, sizeof(magic) - nr_read);
		if (n < 0) {
			// read failed
			(void) close(fd);
			return FTW_STOP;
		}
		nr_read += n;
	}

	if (strncmp(magic, ELFMAG, 4)) {
		// not an ELF
		return FTW_CONTINUE;
	}

	// fd is moved into process
	int ret = process(&global_cfg, fpath, fd);
	if (ret) {
		return FTW_STOP;
	}

	return FTW_CONTINUE;
}

static int process_dir(const char *root)
{
	int ret = nftw(root, &walk_fn, 100, FTW_ACTIONRETVAL | FTW_PHYS);
	if (ret == FTW_STOP) {
		return EX_SOFTWARE;
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
	if (argc != 2) {
		fprintf(
			stderr,
			"usage: %s <sysroot>\n",
			(argc > 0 && argv[0]) ? argv[0] : "shengloong"
		);
		return EX_USAGE;
	}

	const char *from_ver = "GLIBC_2.35";
	const char *new_ver = "GLIBC_2.36";
	struct sl_cfg cfg = {
		.verbose = 1,
		.dry_run = false,

		.from_ver = from_ver,
		.to_ver = new_ver,
		.from_elfhash = bfd_elf_hash(from_ver),
		.to_elfhash = bfd_elf_hash(new_ver),
	};
	global_cfg = cfg;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		errx(EX_SOFTWARE, "libelf initialization failed: %s", elf_errmsg(-1));
	}

	int ret = process_dir(argv[1]);
	if (ret) {
		return ret;
	}

	return 0;
}
