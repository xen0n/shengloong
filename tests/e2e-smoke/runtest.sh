#!/bin/bash

# target shengloong executable to test is passed in argv[1]
sl_prog="$(realpath "$1")"
if [[ $? -ne 0 ]]; then
	dief 'please run the test with `meson test`'
fi

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$mydir/../_common.sh"

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

info "sanity test: can run $old_symver binary in $old_symver sysroot"
run_loong_binary_at_sysroot "$workdir_old" bin/test.old || dief 'sanity check failed'
echo

info "sanity test: can run $new_symver binary in $new_symver sysroot"
run_loong_binary_at_sysroot "$workdir_new" bin/test.new || dief 'sanity check failed'
echo

info "sanity test: cannot run $old_symver binary in $new_symver sysroot"
run_loong_binary_at_sysroot "$workdir_new" bin/test.old && dief 'sanity check failed'
echo

info "sanity test: cannot run $new_symver binary in $old_symver sysroot"
run_loong_binary_at_sysroot "$workdir_old" bin/test.new && dief 'sanity check failed'
echo

info 'update the old sysroot (this time only libs)'
"$sl_prog" -f "GLIBC_$old_symver" -t "GLIBC_$new_symver" -v "$workdir_old/lib64" || dief 'shengloong failed'
echo

info "should no longer be able to run $old_symver binary in the old sysroot"
run_loong_binary_at_sysroot "$workdir_old" bin/test.old && dief 'assertion failed'
echo

info "should now be able to run $new_symver binary in the old sysroot"
run_loong_binary_at_sysroot "$workdir_old" bin/test.new || dief 'assertion failed'
echo

info 'update the old programs'
"$sl_prog" -f "GLIBC_$old_symver" -t "GLIBC_$new_symver" -v "$workdir_old/bin" || dief 'shengloong failed'
echo

info "should now be able to run upgraded $old_symver binary in the old sysroot"
run_loong_binary_at_sysroot "$workdir_old" bin/test.old || dief 'assertion failed'
echo

info 'all passed!'
