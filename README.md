# 昇龍 (Shēnglóng; ShengLoong)

![GitHub Workflow Status (main branch)](https://img.shields.io/github/workflow/status/xen0n/shengloong/meson/main)
![Codecov](https://img.shields.io/codecov/c/gh/xen0n/shengloong)
![GitHub license info](https://img.shields.io/github/license/xen0n/shengloong)

**昇龍** upgrades your LoongArch system with outdated glibc symbol version *in-place*,
to a newer symbol version, so you don't have to re-install from scratch if
glibc 2.36 is released but without the LoongArch port upstreamed.
For everyone else this is pretty much useless, and the project is bound to be
obsoleted in a year or two, after the LoongArch port goes upstream making the
ABI stable.

**昇龍** (Simplified Chinese: 升龙) means "rising dragon", also literally
"upgrade loong", which is exactly what this tool does.

I delibrately chose the traditional characters for the name, even though
most LoongArch users are probably more used to Simplified Chinese, because
the name does not sound "too literal" this way. Plus, the "rising sun"
connotation seems good for such an emerging architecture.

## Installation

### Gentoo

**昇龍** is packaged in the [loongson-overlay](https://github.com/xen0n/loongson-overlay),
and should be available in the main tree shortly.

```sh
emerge app-portage/shengloong
```

### From source

**昇龍** should work on any POSIX system.

First make sure the dependencies are available. The dependencies are:

* **libelf** for handling ELF files, obviously
* **popt** for parsing CLI options
* **meson** as the build system
* preferably **ninja** as the fast Make replacement

Any version would work, but make sure you have the development headers/libs
in place too if your distribution packages them separately.


```sh
# Debian and derivatives (e.g. Ubuntu)
sudo apt-get install libelf-dev libpopt-dev meson ninja-build

# Gentoo and derivatives; meson and ninja should already be present.
sudo emerge virtual/libelf dev-libs/popt
```

Then build like any other Meson project.

```sh
meson setup <builddir>  # replace with your preferred name for a build dir
ninja -C <builddir>
```

The executable should be available at `<builddir>/shengloong`.

## Usage

**昇龍** can be used to migrate LoongArch sysroots from *any architecture*, not
just limited to native operation.
As atomic replacing of files is not yet implemented, currently you have to
reboot into another system for migrating your existing sysroot.
(This is planned for the next minor version.)

**Warning**. This program has no warranty, like almost every free software.

**ALWAYS BACKUP BEFORE PROCEEDING.**<br />
**ALWAYS BACKUP BEFORE PROCEEDING.**<br />
**ALWAYS BACKUP BEFORE PROCEEDING.**

```
Usage: shengloong <root dirs>
  -v, --verbose                 produce more (debugging) output
  -p, --pretend                 don't actually patch the files
  -f, --from-ver=GLIBC_2.3x     migrate from this glibc symbol version
                                (default: "GLIBC_2.35")
  -t, --to-ver=GLIBC_2.3y       migrate to this glibc symbol version (default:
                                "GLIBC_2.36")

Help options:
  -?, --help                    Show this help message
      --usage                   Display brief usage message
```

Examples:

```sh
# see what needs to be done but not actually patching anything
sudo shengloong -p /path/to/sysroot

# execute the migration
sudo shengloong /path/to/sysroot

# in case we have to migrate again to GLIBC_2.37 from a 2.36 sysroot
sudo shengloong -f GLIBC_2.36 -t GLIBC_2.37 /path/to/sysroot

# you could migrate multiple sysroots in one invocation
sudo shengloong /sysroot/a /sysroot/b
```

## License

This project is licensed under GPL, version 3 or later.
