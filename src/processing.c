#include <endian.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <gelf.h>

#include "elfcompat.h"
#include "processing.h"
#include "processing_ldso.h"
#include "utils.h"

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
		fprintf(stderr, "elf_begin on %s (fd %d) failed: %s\n", path, fd, elf_errmsg(-1));
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
		return 0;
	}

	// only process little-endian files for now
	if (ident[EI_DATA] != ELFDATA2LSB) {
		return 0;
	}

	Elf64_Ehdr *ehdr = elf64_getehdr(e);

	// only process LoongArch files
	if (le16toh(ehdr->e_machine) != EM_LOONGARCH) {
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
				return EX_SOFTWARE;  // GCOVR_EXCL_LINE: virtually impossible
			}

			const char *scn_name;
			if ((scn_name = elf_strptr(e, shstrndx, shdr.sh_name)) == NULL) {
				return EX_SOFTWARE;  // GCOVR_EXCL_LINE: virtually impossible
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
		while ((void *)sym < (void *)((uint8_t *)d->d_buf + d->d_size)) {
			if (ELF64_ST_TYPE(sym->st_info) != STT_OBJECT) {
				// STT_FUNC names are stored without version information,
				// so only look at STT_OBJECT symbols
				goto next_sym;
			}

			// it seems version symbols are all stored with STN_ABS
			if (le16toh(sym->st_shndx) != SHN_ABS) {
				goto next_sym;
			}

			Elf64_Word st_name = le32toh(sym->st_name);
			const char *ver_name = sl_elf_dynstr(ctx, st_name);
			if (ctx->cfg->verbose) {
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
			Elf64_Verdaux *aux = (Elf64_Verdaux *)((uint8_t *)vd + le32toh(vd->vd_aux));

			// only look at the first aux, because this aux is the vd's name
			Elf64_Word vda_name = le32toh(aux->vda_name);
			const char *vda_name_str = sl_elf_dynstr(ctx, vda_name);
			if (ctx->cfg->verbose) {
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
			int ret = sl_elf_patch_dynstr_by_idx(ctx, vda_name, ctx->cfg->to_ver);
			if (ret) {
				return ret;
			}

			// patch hash
			if (le32toh(vd->vd_hash) != ctx->cfg->to_elfhash) {
				vd->vd_hash = htole32(ctx->cfg->to_elfhash);
				elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
				ctx->dirty = true;
			}

next:
			i++;
			vd = (Elf64_Verdef *)((uint8_t *)vd + le32toh(vd->vd_next));
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
			if (ctx->cfg->verbose) {
				const char *dep_filename = sl_elf_raw_dynstr(ctx, le32toh(vn->vn_file));
				printf("%s: verneed %zd: depending on %s\n", ctx->path, i, dep_filename);
			}

			size_t j = 0;
			Elf64_Vernaux *aux = (Elf64_Vernaux *)((uint8_t *)vn + le32toh(vn->vn_aux));
			for (j = 0; j < vn->vn_cnt; j++, aux = (Elf64_Vernaux *)((uint8_t *)aux + le32toh(aux->vna_next))) {
				Elf64_Word vna_name = le32toh(aux->vna_name);
				const char *vna_name_str = sl_elf_raw_dynstr(ctx, vna_name);
				if (ctx->cfg->verbose) {
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
				int ret = sl_elf_patch_dynstr_by_off(ctx, vna_name, ctx->cfg->to_ver);
				if (ret) {
					return ret;
				}

				// patch hash
				if (le32toh(aux->vna_hash) != ctx->cfg->to_elfhash) {
					aux->vna_hash = htole32(ctx->cfg->to_elfhash);
					elf_flagdata(d, ELF_C_SET, ELF_F_DIRTY);
					elf_flagscn(s, ELF_C_SET, ELF_F_DIRTY);
					ctx->dirty = true;
				}
			}

			i++;
			vn = (Elf64_Verneed *)((uint8_t *)vn + le32toh(vn->vn_next));
		}
	}

	return 0;
}
