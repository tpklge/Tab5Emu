/* SPDX-License-Identifier: 0BSD; diagnostic program written for this project. */
.syntax unified
.cpu arm7tdmi
.arm
.section .header,"ax",%progbits
.global _start
_start:
    b reset
    .space 188, 0
.section .text.start,"ax",%progbits
reset:
    ldr sp, =0x03007f00
    bl main
1:  b 1b
