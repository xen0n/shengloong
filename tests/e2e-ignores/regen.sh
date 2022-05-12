#!/bin/sh

CC_RISCV64="${CC_RISCV64:-riscv64-unknown-linux-gnu-gcc}"
STRIP_RISCV64="${STRIP_RISCV64:-riscv64-unknown-linux-gnu-strip}"
CC_MIPS64EB="${CC_MIPS64EB:-mips64-unknown-linux-gnu-gcc}"
STRIP_MIPS64EB="${STRIP_MIPS64EB:-mips64-unknown-linux-gnu-strip}"
CC_MIPS64EL="${CC_MIPS64EL:-mips64el-unknown-linux-gnu-gcc}"
STRIP_MIPS64EL="${STRIP_MIPS64EL:-mips64el-unknown-linux-gnu-strip}"

"$CC_MIPS64EB" -O3 -mabi=64 -march=mips64r2 -EB -o bin/hello.mipsn64eb ./hello.c
"$STRIP_MIPS64EB" bin/hello.mipsn64eb
"$CC_MIPS64EL" -O3 -mabi=32 -march=mips64r2 -EL -o bin/hello.mipso32 ./hello.c
"$STRIP_MIPS64EL" bin/hello.mipso32
"$CC_MIPS64EL" -O3 -mabi=n32 -march=mips64r2 -EL -o bin/hello.mipsn32 ./hello.c
"$STRIP_MIPS64EL" bin/hello.mipsn32
"$CC_MIPS64EL" -O3 -mabi=64 -march=mips64r2 -EL -o bin/hello.mipsn64 ./hello.c
"$STRIP_MIPS64EL" bin/hello.mipsn64
"$CC_RISCV64" -O3 -mabi=lp64d -march=rv64gc -o bin/hello.rv64 ./hello.c
"$STRIP_RISCV64" bin/hello.rv64
