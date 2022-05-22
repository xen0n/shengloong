#!/bin/bash

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$mydir/_common.sh"

if [[ $# -ne 1 ]]; then
  dief 'usage: %s <target shengloong executable to test>' "$0"
fi

# target shengloong executable to test is passed in argv[1]
sl_prog="$(realpath "$1")"

info 'calling with no arguments -- should print usage and bail'
"$sl_prog" > /dev/null 2>&1 && dief 'should fail'
"$sl_prog" 2>&1 | grep -i usage > /dev/null || dief 'should print usage'

info 'calling with -? -- should print usage'
"$sl_prog" '-?' | grep -i usage > /dev/null || dief 'should print usage'

info 'calling with --help -- should print usage'
"$sl_prog" --help | grep -i usage > /dev/null || dief 'should print usage'

info 'calling with no positional args -- should bail'
"$sl_prog" -t GLIBC_2.36 > /dev/null 2>&1 && dief 'should fail'

info 'calling with unknown parameter -- should bail'
"$sl_prog" --foo bar /dev > /dev/null 2>&1 && dief 'should fail'

info 'all passed!'
