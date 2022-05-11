#!/bin/sh

CC_RISCV64="${CC_RISCV64:-riscv64-unknown-linux-gnu-gcc}"
STRIP_RISCV64="${STRIP_RISCV64:-riscv64-unknown-linux-gnu-strip}"
CC_MIPS="${CC_MIPS:-mips64el-unknown-linux-gnu-gcc}"
STRIP_MIPS="${STRIP_MIPS:-mips64el-unknown-linux-gnu-strip}"

"$CC_MIPS" -O3 -mabi=32 -march=mips64r2 -EL -o bin/hello.mipso32 ./hello.c
"$STRIP_MIPS" bin/hello.mipso32
"$CC_MIPS" -O3 -mabi=n32 -march=mips64r2 -EL -o bin/hello.mipsn32 ./hello.c
"$STRIP_MIPS" bin/hello.mipsn32
"$CC_MIPS" -O3 -mabi=64 -march=mips64r2 -EL -o bin/hello.mipsn64 ./hello.c
"$STRIP_MIPS" bin/hello.mipsn64
"$CC_RISCV64" -O3 -mabi=lp64d -march=rv64gc -o bin/hello.rv64 ./hello.c
"$STRIP_RISCV64" bin/hello.rv64
