#!/bin/bash

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$mydir/../_common.sh"

if [[ $# -ne 1 ]]; then
  dief 'usage: %s <target shengloong executable to test>' "$0"
fi

# target shengloong executable to test is passed in argv[1]
sl_prog="$(realpath "$1")"

old_symver=2.35
new_symver=2.36

progs_dir="$mydir/bin"

workdir="$(mktemp -d)"

cleanup() {
  dbgrun rm -rf "$workdir" || true
}

trap cleanup EXIT

dbgf 'setting up workdir'
cp -r "$progs_dir"/* "$workdir" || dief 'cp failed'

info 'update the workdir'
"$sl_prog" -f "GLIBC_$old_symver" -t "GLIBC_$new_symver" -v "$workdir" || dief 'shengloong failed'
echo

info 'checksumming -- all should stay intact'
assert_sha256sum e07ea1c69ab3f57fb4bf681d3ff8d1aec31e838af27eb2b5fbcadc1705753377 "$workdir/hello.mipsn32"
assert_sha256sum b1d625d4f30eae709893d13c11f0af47934b51dece6036715e762a487c7c0b3c "$workdir/hello.mipsn64"
assert_sha256sum 3750b274cd9753818b7077d11b490649a35c35a926c5c77d9737a052528b1f77 "$workdir/hello.mipsn64eb"
assert_sha256sum 6a45f3fdacd2c13905ec74f6f5ce4dfdfa35ce2a44eb8597143099363d5aa3bf "$workdir/hello.mipso32"
assert_sha256sum 780a51e63a63e9beb0dd6483fd6b2cbd826d42f19aa000be727710e76e6ecca2 "$workdir/hello.rv64"

info 'all passed!'
