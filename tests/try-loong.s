# Source code to the tiny helper "try-loong", simplest LoongArch program
# possible to return success, for indicating the current system can load and
# run LoongArch ELF binaries.

.text

.global _start
_start:
	li.w    $a0, 0
	li.w    $a7, 94  # exit_group
	syscall 0

.section .note.GNU-stack,"",@progbits
