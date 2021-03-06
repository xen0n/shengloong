#include <fcntl.h>
#include <ftw.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "gettext.h"
#include "processing.h"

#define _(x) gettext(x)

static int walk_fn(
    const char *fpath,
    const struct stat *sb,
    int typeflag,
    struct FTW *ftwbuf __attribute__((unused)))
{
    if (typeflag != FTW_F) {
        return FTW_CONTINUE;
    }

    if ((sb->st_mode & S_IFMT) != S_IFREG) {
        // we're only interested in regular files
        return FTW_CONTINUE;
    }

    if ((size_t)sb->st_size < sizeof(Elf64_Ehdr)) {
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
    size_t nr_read = 0;
    while (nr_read < sizeof(magic)) {
        ssize_t n = read(fd, magic + nr_read, sizeof(magic) - nr_read);
        if (n < 0) {
            // read failed
            // GCOVR_EXCL_START: unlikely to happen except like media error, given open(2) already succeeded
            (void) close(fd);
            return FTW_STOP;
            // GCOVR_EXCL_STOP
        }
        if (n == 0) {
            // cannot get more data; will happen on special files such as some
            // pseudo files from /sys
            break;
        }
        nr_read += (size_t)n;
    }

    if (nr_read < sizeof(magic)) {
        // definitely not an ELF
        (void) close(fd);
        return FTW_CONTINUE;
    }

    if (strncmp(magic, ELFMAG, 4)) {
        // not an ELF
        (void) close(fd);
        return FTW_CONTINUE;
    }

    // fd is moved into process
    int ret = process(&global_cfg, fpath, fd);
    if (ret) {
        // better to continue with the remaining files
        return FTW_CONTINUE;
    }

    return FTW_CONTINUE;
}

int process_dir(const char *root)
{
    int ret = nftw(root, &walk_fn, 100, FTW_ACTIONRETVAL | FTW_PHYS);
    if (ret == FTW_STOP) {
        return EX_SOFTWARE;
    }

    return 0;
}
