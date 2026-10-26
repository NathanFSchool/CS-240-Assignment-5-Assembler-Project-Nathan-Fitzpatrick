# Reconstructed from MHC1 version 1.
# Text: 0x00400000; data: 0x10010000.
# MARS/SPIM syscall conventions; delayed branching disabled.
# Entry: main. Original label names and comments are not stored.

.data
    .byte 0x46, 0x69, 0x7A, 0x7A, 0x00, 0x42, 0x75, 0x7A, 0x7A, 0x00

.text
.globl main

main:
    addi $t0, $zero, 9
    addi $t1, $zero, 3
    addi $t2, $zero, 5
    div $t0, $t1
    mfhi $t3
    beq $t3, $zero, L_00400028

L_00400018:
    div $t0, $t2
    mfhi $t3
    beq $t3, $zero, L_0040003C
    j L_0040004C

L_00400028:
    addi $v0, $zero, 4
    lui $a0, 0x1001
    ori $a0, $a0, 0x0000
    syscall
    j L_00400018

L_0040003C:
    addi $v0, $zero, 4
    lui $a0, 0x1001
    ori $a0, $a0, 0x0005
    syscall

L_0040004C:
    addi $v0, $zero, 10
    syscall
