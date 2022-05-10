#!/bin/bash

# this is meant to be sourced; shebang line is for editors to properly
# highlight bashisms

_common_tool_dir="$(cd "$(dirname "$BASH_SOURCE[0]")" && pwd)"

dief() {
	local fmt="$1"
	shift
	
	printf "fatal: ${fmt}\n" "$@" >&2
	exit 1
}

dbgrun() {
	if [[ -n $DEBUG_TESTS ]]; then
		echo "debug: running:" "$@"
		echo "            in:" "$(pwd)"
	fi

	"$@"
}

dbgf() {
	if [[ -z $DEBUG_TESTS ]]; then
		return 0
	fi

	local fmt="$1"
	shift
	
	printf "debug: ${fmt}\n" "$@"
}

info() {
	echo 'info:' "$@"
}

infof() {
	local fmt="$1"
	shift
	
	printf "info: ${fmt}\n" "$@"
}

run_loong_binary_at_sysroot() {
	local sysroot="$1"
	shift
	local prog="$1"
	shift
	local ret

	# check for qemu-loongarch64, don't run in chroot but rather use -L for
	# avoiding having to copy qemu-loongarch64 into the sysroot
	if command -v qemu-loongarch64 > /dev/null; then
		pushd "$sysroot" > /dev/null || dief 'cannot pushd into %s' "$sysroot"
			dbgrun qemu-loongarch64 -L "$sysroot" "$sysroot/$prog" "$@"
			ret=$?
		popd > /dev/null || dief 'cannot popd'
		return $ret
	fi

	# check if we can directly execute LoongArch binaries
	# this implies more native environment than qemu + binfmt_misc, because
	# that setup will break in our case (not sure if qemu-loongarch64 is
	# static; emulator is not copied into sysroot)
	if "$_common_tool_dir/try-loong"; then
		pushd "$sysroot" > /dev/null || dief 'cannot pushd into %s' "$sysroot"
			LD_LIBRARY_PATH="$sysroot/lib64" dbgrun "$sysroot/$prog" "$@"
			ret=$?
		popd > /dev/null || dief 'cannot popd'
		return $ret
	fi

	# the test cannot be run on the current system, skip the whole case
	echo "warning: unable to run loongarch64 binaries, skipping test" >&2
	echo "note: install qemu-loongarch64 or use native hardware" >&2
	exit 77
}
