#!/bin/bash

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$mydir/../_common.sh"

if [[ $# -ne 1 ]]; then
  dief 'usage: %s <target shengloong executable to test>' "$0"
fi

# target shengloong executable to test is passed in argv[1]
sl_prog="$(realpath "$1")"

workdir="$(mktemp -d)"

cleanup() {
  dbgrun rm -rf "$workdir" || true
}

trap cleanup EXIT

dbgf 'setting up workdir'
cp -r "$mydir"/*.a "$mydir"/*.bin "$workdir" || dief 'cp failed'

# now for some fun: cycles!
pushd "$workdir" > /dev/null || dief 'pushd failed'
  mkdir a b c d || dief 'mkdir failed'
  ln -s ../b ./a/next || dief 'symlink failed'
  ln -s ../c ./b/next || dief 'symlink failed'
  ln -s ../a ./c/next || dief 'symlink failed'
  ln -s . ./d/self || dief 'symlink failed'
popd > /dev/null || dief 'popd failed'

info 'dry-running on /dev for lots of special files'
"$sl_prog" -p /dev
echo "retcode = $?; passes on some systems while failing on others, that's fine"

info 'dry-running on /sys for unreadable and special files'
"$sl_prog" -p /sys && dief 'should fail due to unreadable files'

info "real-running on $workdir"
"$sl_prog" "$workdir" || dief 'should pass'

info 'checksumming; nothing should change'
assert_sha256sum e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 "$workdir/empty.bin"
assert_sha256sum 3f2edf04e53a79b41a55ebd118476c78187a08ea5367085075293dd39bc74093 "$workdir/libm.a"
assert_sha256sum f0a17a43c74d2fe5474fa2fd29c8f14799e777d7d75a2cc4d11c20a6e7b161c5 "$workdir/librt.a"
assert_sha256sum 0eb9e3089dc8479fdc76d897a20c1555c51505d9f13cc97a868af3ef5988dc87 "$workdir/notelf-a.bin"
assert_sha256sum 73cb3858a687a8494ca3323053016282f3dad39d42cf62ca4e79dda2aac7d9ac "$workdir/notelf-b.bin"
assert_sha256sum 3bdbb4fe8397cd2b842430b39ccff01a8663c751945ef5e9a09e267fb8b1d359 "$workdir/notelf-c.bin"

info 'all passed!'
