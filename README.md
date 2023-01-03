# 昇龍 (Shēnglóng; ShengLoong)

![GitHub Workflow Status (main branch)](https://img.shields.io/github/workflow/status/xen0n/shengloong/meson/main)
![Codecov](https://img.shields.io/codecov/c/gh/xen0n/shengloong)
![GitHub license info](https://img.shields.io/github/license/xen0n/shengloong)

**昇龍** upgrades your LoongArch system with outdated glibc symbol version *in-place*,
to `GLIBC_2.36`, and checks your system for other obsolete ABI features so
your system stays on the bleeding-edge upstreamed ABI without having to get
reinstalled from time to time.
For everyone else this is pretty much useless, and the project is bound to be
obsoleted in a year or two, after the LoongArch ABI fully matures.

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
  -t, --to-ver=STRING           deprecated; no effect now
  -a, --check-syscall-abi       scan for syscall ABI incompatibility, don't
                                patch files
  -o, --check-objabi            scan for obsolete object file ABI usage, don't
                                patch files

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

# after the migration, you may also check if you have to get rid of newfstatat
# usage in your system
sudo shengloong -a /path/sysroot

# you could also migrate multiple sysroots in one invocation
sudo shengloong /sysroot/a /sysroot/b

# for fresher installations (those after 2022-08 but before early 2023), you
# could preemptively check for lingering object file ABI v0 usage, to avoid
# having problems with newer upstream toolchain components such as lld or mold
sudo shengloong -o /path/to/sysroot
```

The examples are `sudo`-prefixed to avoid having insufficient permissions
while reading certain privileged executables and/or libraries.

## License

This project is licensed under GPL, version 3 or later.
