#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>

#include "cfg.h"
#include "processing_ldso.h"

int patch_ldso_rodata(struct sl_elf_ctx *ctx, Elf_Scn *s)
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

int patch_ldso_text_hashes(struct sl_elf_ctx *ctx, Elf_Scn *s)
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
