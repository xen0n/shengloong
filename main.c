#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

#include <libelf.h>

#ifndef EM_LOONGARCH
#define EM_LOONGARCH 258
#endif

static int process_elf(Elf *e);
static int process(const char *path);

static int process(const char *path)
{
	int fd;
	Elf *e;
	int ret = 0;

	fd = open(path, O_RDONLY, 0);
	if (fd < 0) {
		return EX_NOINPUT;
	}

	e = elf_begin(fd, ELF_C_RDWR, NULL);
	if (!e) {
		fprintf(stderr, "elf_begin on %s (fd %d) failed: %s\n", path, fd, elf_errmsg(-1));
		goto close;
	}

	switch (elf_kind(e)) {
	case ELF_K_AR:
		// TODO
		break;

	case ELF_K_ELF:
		ret = process_elf(e);
		break;

	default:
		// not handled
		break;
	}

	(void) elf_end(e);
	(void) close(fd);

	return ret;

close:
	(void) close(fd);
	return EX_SOFTWARE;
}

static int process_elf(Elf *e)
{
	size_t len;
	const char *ident;
	Elf64_Ehdr *ehdr;

	ident = elf_getident(e, &len);
	if (len != EI_NIDENT) {
		return EX_SOFTWARE;
	}

	// only process ELF64 files for now
	if (ident[EI_CLASS] != ELFCLASS64) {
		return 0;
	}

	ehdr = elf64_getehdr(e);

	// debug
	printf("e_machine = %d\n", ehdr->e_machine);

	// only process LoongArch files
	if (ehdr->e_machine != EM_LOONGARCH) {
		return 0;
	}

	printf("xxx\n");

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
		ret = process(argv[i]);
		if (ret) {
			return ret;
		}
	}

	return 0;
}
