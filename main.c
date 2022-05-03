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

static int process_elf(const char *path, Elf *e, bool dry_run);
static int process(const char *path, bool dry_run);

static int process(const char *path, bool dry_run)
{
	int fd;
	Elf *e;
	int ret = 0;

	fd = open(path, O_RDONLY, 0);
	if (fd < 0) {
		return EX_NOINPUT;
	}

	e = elf_begin(fd, dry_run ? ELF_C_READ_MMAP : ELF_C_RDWR_MMAP, NULL);
	if (!e) {
		fprintf(stderr, "elf_begin on %s (fd %d) failed: %s\n", path, fd, elf_errmsg(-1));
		goto close;
	}

	switch (elf_kind(e)) {
	case ELF_K_ELF:
		ret = process_elf(path, e, dry_run);
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

static int process_elf(const char *path, Elf *e, bool dry_run)
{
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
	Elf_Scn *s_gnu_version = NULL;
	Elf_Scn *s_gnu_version_r = NULL;
	{
		Elf_Scn *scn = NULL;
		while ((scn = elf_nextscn(e, scn)) != NULL) {
			GElf_Shdr shdr;
			if (gelf_getshdr(scn, &shdr) != &shdr) {
				return EX_SOFTWARE;
			}

			const char *scn_name;
			if ((scn_name = elf_strptr(e, shstrndx, shdr.sh_name)) == NULL) {
				return EX_SOFTWARE;
			}

			if (!strncmp(".dynsym", scn_name, 7)) {
				s_dynsym = scn;
				continue;
			}
			// this must come first because .gnu.version is prefix of this
			if (!strncmp(".gnu.version_r", scn_name, 14)) {
				s_gnu_version_r = scn;
				continue;
			}
			if (!strncmp(".gnu.version", scn_name, 12)) {
				s_gnu_version = scn;
				continue;
			}
		}
	}

	(void) printf("%s: .dynsym        at %p\n", path, s_dynsym);
	(void) printf("%s: .gnu.version   at %p\n", path, s_gnu_version);
	(void) printf("%s: .gnu.version_r at %p\n", path, s_gnu_version_r);

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

	if (elf_version(EV_CURRENT) == EV_NONE) {
		errx(EX_SOFTWARE, "libelf initialization failed: %s", elf_errmsg(-1));
	}

	for (i = 1; i < argc; i++) {
		// TODO: non-dry-run
		ret = process(argv[i], true);
		if (ret) {
			return ret;
		}
	}

	return 0;
}
