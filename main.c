#include <err.h>
#include <fcntl.h>
#include <ftw.h>
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

#include "cfg.h"
#include "ctx.h"
#include "elfcompat.h"
#include "processing.h"
#include "utils.h"

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

static void
__attribute__((noreturn))
usage(poptContext pctx, int exitcode, const char *error)
{
	poptPrintUsage(pctx, stderr, 0);
	if (error) {
		fprintf(stderr, "%s\n", error);
	}
	exit(exitcode);
}

#define DEFAULT_FROM "GLIBC_2.35"
#define DEFAULT_TO "GLIBC_2.36"

int main(int argc, const char *argv[])
{
	struct sl_cfg cfg = {
		.verbose = false,
		.dry_run = false,

		.from_ver = DEFAULT_FROM,
		.to_ver = DEFAULT_TO,
	};

	struct poptOption options[] = {
		{ "verbose", 'v', POPT_ARG_NONE, &cfg.verbose, 0, "produce more (debugging) output", NULL },
		{ "pretend", 'p', POPT_ARG_NONE, &cfg.dry_run, 0, "don't actually patch the files", NULL },
		{ "from-ver", 'f', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &cfg.from_ver, 0, "migrate from this glibc symbol version", "GLIBC_2.3x" },
		{ "to-ver", 't', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &cfg.to_ver, 0, "migrate to this glibc symbol version", "GLIBC_2.3y" },
		POPT_AUTOHELP
		POPT_TABLEEND
	};

	poptContext pctx = poptGetContext(NULL, argc, argv, options, 0);
	poptSetOtherOptionHelp(pctx, "<root dirs>");
	if (argc < 2) {
		poptPrintUsage(pctx, stderr, 0);
		exit(EX_USAGE);
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
		usage(pctx, EX_USAGE, "at least one directory argument is required");
	}

	cfg.from_elfhash = bfd_elf_hash(cfg.from_ver);
	cfg.to_elfhash = bfd_elf_hash(cfg.to_ver);

	global_cfg = cfg;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		errx(EX_SOFTWARE, "libelf initialization failed: %s", elf_errmsg(-1));
	}

	const char *dir;
	while ((dir = poptGetArg(pctx)) != NULL) {
		ret = process_dir(dir);
		if (ret) {
			return ret;
		}
	}

	poptFreeContext(pctx);

	return 0;
}
