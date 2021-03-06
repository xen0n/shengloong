#!/bin/bash

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$mydir/../_common.sh"

if [[ $# -ne 1 ]]; then
  dief 'usage: %s <target shengloong executable to test>' "$0"
fi

# don't let i18n interfere with string matches
export LC_ALL=C.UTF-8
export LANG=C.UTF-8

# target shengloong executable to test is passed in argv[1]
sl_prog="$(realpath "$1")"

old_symver=2.35
new_symver=2.36

test_prog=ls
test_prog_old="$mydir/$test_prog-$old_symver"
test_prog_new="$mydir/$test_prog-$new_symver"
sysroot_old="$mydir/sysroot-$old_symver"
sysroot_new="$mydir/sysroot-$new_symver"

workdir_old="$(mktemp -d)"
workdir_new="$(mktemp -d)"

dbgf 'workdir_old = %s' "$workdir_old"
dbgf 'workdir_new = %s' "$workdir_new"

cleanup() {
  dbgrun rm -rf "$workdir_old" "$workdir_new" || true
}

trap cleanup EXIT

dbgf 'setting up workdir for old symver %s' "$old_symver"
cp -r "$sysroot_old"/* "$workdir_old" || dief 'cp failed'
mkdir "$workdir_old/bin" || dief 'mkdir failed'
cp "$test_prog_old" "$workdir_old/bin/test.old" || dief 'cp failed'
cp "$test_prog_new" "$workdir_old/bin/test.new" || dief 'cp failed'

dbgf 'setting up workdir for new symver %s' "$new_symver"
cp -r "$sysroot_new"/* "$workdir_new" || dief 'cp failed'
mkdir "$workdir_new/bin" || dief 'mkdir failed'
cp "$test_prog_old" "$workdir_new/bin/test.old" || dief 'cp failed'
cp "$test_prog_new" "$workdir_new/bin/test.new" || dief 'cp failed'

# The Gentoo sandbox can't itself work in sysroots with different glibc
# symver, so the sanity will fail even if running natively, during normal
# emerges of this package.
#
# We assume this case and fall back to blindly running and comparing
# checksums, if the sanity check fails. Unexpected outputs can still be caught
# this way.
should_run_progs=true

if [[ $CI == true ]]; then
  # don't attempt to run LoongArch binaries on CI, due to no qemu binary
  # package readily available, and no native LoongArch CI boxes in the
  # foreseeable future (when this project is still relevant; *if* LoongArch
  # were to become popular enough to be available in GitHub Actions, the
  # ecosystem would have long matured, and that includes stable glibc
  # symver, making this project's existence unnecessary in the first place.)
  info "detected CI environment, only doing checksumming"
  should_run_progs=false
else
  info "sanity test: can run $old_symver binary in $old_symver sysroot"
  if ! run_loong_binary_at_sysroot "$workdir_old" bin/test.old; then
    info 'cannot run old-symver binary with matching sysroot, we might be in a Gentoo sandbox'
    info 'falling back to checksumming only'
    should_run_progs=false
  fi
fi

if "$should_run_progs"; then
  info "sanity test: can run $new_symver binary in $new_symver sysroot"
  run_loong_binary_at_sysroot "$workdir_new" bin/test.new || dief 'sanity check failed'
  echo

  info "sanity test: cannot run $old_symver binary in $new_symver sysroot"
  run_loong_binary_at_sysroot "$workdir_new" bin/test.old && dief 'sanity check failed'
  echo

  info "sanity test: cannot run $new_symver binary in $old_symver sysroot"
  run_loong_binary_at_sysroot "$workdir_old" bin/test.new && dief 'sanity check failed'
  echo
fi

info 'test dry-run on the old sysroot'
"$sl_prog" -f "GLIBC_$old_symver" -t "GLIBC_$new_symver" -p "$workdir_old" || dief 'shengloong failed'
echo

info 'update the old sysroot (this time only libs)'
"$sl_prog" -f "GLIBC_$old_symver" -t "GLIBC_$new_symver" -v "$workdir_old/lib64" || dief 'shengloong failed'
echo

# both should be modified
assert_sha256sum 55a4118c07340126be958cc9f5e24d5f1a7966590b6a2d0e315166c4e326ddc6 "$workdir_old/lib64/ld-linux-loongarch-lp64d.so.1"
assert_sha256sum baa94e1bdd823404cb6b4ba23c356c262f70f0288df3060c9fc4771c8c337e05 "$workdir_old/lib64/libc.so.6"

if "$should_run_progs"; then
  info "should no longer be able to run $old_symver binary in the old sysroot"
  run_loong_binary_at_sysroot "$workdir_old" bin/test.old && dief 'assertion failed'
  echo

  info "should now be able to run $new_symver binary in the old sysroot"
  run_loong_binary_at_sysroot "$workdir_old" bin/test.new || dief 'assertion failed'
  echo
fi

info 'update the old programs'
"$sl_prog" -f "GLIBC_$old_symver" -t "GLIBC_$new_symver" -v "$workdir_old/bin" || dief 'shengloong failed'
echo

# this file should get modified
assert_sha256sum 216943dcfe25a2f4a79043558fe6642cbf068dfb0699684c3ff29e4664ef6e56 "$workdir_old/bin/test.old"
# this file should stay intact
assert_sha256sum 2e63e339621de1b742be8ac710c68bff4442bef4928bd65b62593f97592bc2c0 "$workdir_old/bin/test.new"

if "$should_run_progs"; then
  info "should now be able to run upgraded $old_symver binary in the old sysroot"
  run_loong_binary_at_sysroot "$workdir_old" bin/test.old || dief 'assertion failed'
  echo
fi

info 'update the whole new sysroot'
# this time without enable verbose output for more thorough branch coverage
"$sl_prog" -f "GLIBC_$old_symver" -t "GLIBC_$new_symver" "$workdir_new" || dief 'shengloong failed'
echo

# the sysroot itself should stay intact
assert_sha256sum 08981ad5c367335187971f89b96510f5daa33746eae65c6b099a8f7d4b9b744f "$workdir_new/lib64/ld-linux-loongarch-lp64d.so.1"
assert_sha256sum 78e7c5064219dedb5c7bacaf73b8ad63da155d92bee174c9b73d00b0dc8c15ff "$workdir_new/lib64/libc.so.6"

# result should be deterministic
assert_sha256sum 216943dcfe25a2f4a79043558fe6642cbf068dfb0699684c3ff29e4664ef6e56 "$workdir_new/bin/test.old"
assert_sha256sum 2e63e339621de1b742be8ac710c68bff4442bef4928bd65b62593f97592bc2c0 "$workdir_new/bin/test.new"

if "$should_run_progs"; then
  info "should now be able to run upgraded $old_symver binary in the new sysroot"
  run_loong_binary_at_sysroot "$workdir_new" bin/test.old || dief 'assertion failed'
  echo

  info "$new_symver binary in the new sysroot shouldn't get broken"
  run_loong_binary_at_sysroot "$workdir_new" bin/test.new || dief 'assertion failed'
  echo
fi

info 'even the new sysroot has deprecated syscall usages'
stdout="$("$sl_prog" -a "$workdir_new")"
[[ $? -ne 0 ]] && dief 'shengloong -a failed'
echo "$stdout" | grep 'lib64/libc\.so\.6: usage of removed syscall `newfstatat` at \.text+0xb37f8$' || dief 'expected to see .text+0xb37f8 being called out'
echo "$stdout" | grep 'lib64/ld-linux-loongarch-lp64d\.so\.1: usage of removed syscall `newfstatat` at \.text+0x1bb64$' || dief 'expected to see .text+0x1bb64 being called out'

echo

info 'all passed!'
