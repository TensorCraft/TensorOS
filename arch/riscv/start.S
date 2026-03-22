    .section .init, "ax", @progbits
    .globl _start
    .globl __mtvec_base
    .globl __trap_entry
    .globl preempt_return_trampoline

    .extern g_trap_preempt_resume_pc
    .extern process_yield

_start:
    la sp, _stack_top
    /* Keep gp initialization intact; linker relaxation can otherwise erase it. */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    call boot_main
3:
    j 3b

    .section .exception_vectors_table.text, "ax", @progbits
    .balign 0x100
    .option push
    .option norvc
    .type __mtvec_base, @function
__mtvec_base:
    j __trap_entry
    j __vector_interrupt_1
    j __vector_interrupt_2
    j __vector_interrupt_3
    j __vector_interrupt_4
    j __vector_interrupt_5
    j __vector_interrupt_6
    j __vector_interrupt_7
    j __vector_interrupt_8
    j __vector_interrupt_9
    j __vector_interrupt_10
    j __vector_interrupt_11
    j __vector_interrupt_12
    j __vector_interrupt_13
    j __vector_interrupt_14
    j __vector_interrupt_15
    .rept 16
    j __trap_entry
    .endr
    .size __mtvec_base, . - __mtvec_base
    .option pop

    .macro SAVE_ALL
    addi sp, sp, -128
    sw ra,   0(sp)
    sw gp,   4(sp)
    sw tp,   8(sp)
    sw t0,  12(sp)
    sw t1,  16(sp)
    sw t2,  20(sp)
    sw s0,  24(sp)
    sw s1,  28(sp)
    sw a0,  32(sp)
    sw a1,  36(sp)
    sw a2,  40(sp)
    sw a3,  44(sp)
    sw a4,  48(sp)
    sw a5,  52(sp)
    sw a6,  56(sp)
    sw a7,  60(sp)
    sw s2,  64(sp)
    sw s3,  68(sp)
    sw s4,  72(sp)
    sw s5,  76(sp)
    sw s6,  80(sp)
    sw s7,  84(sp)
    sw s8,  88(sp)
    sw s9,  92(sp)
    sw s10, 96(sp)
    sw s11,100(sp)
    sw t3, 104(sp)
    sw t4, 108(sp)
    sw t5, 112(sp)
    sw t6, 116(sp)
    .endm

    .macro RESTORE_ALL
    lw ra,   0(sp)
    lw gp,   4(sp)
    lw tp,   8(sp)
    lw t0,  12(sp)
    lw t1,  16(sp)
    lw t2,  20(sp)
    lw s0,  24(sp)
    lw s1,  28(sp)
    lw a0,  32(sp)
    lw a1,  36(sp)
    lw a2,  40(sp)
    lw a3,  44(sp)
    lw a4,  48(sp)
    lw a5,  52(sp)
    lw a6,  56(sp)
    lw a7,  60(sp)
    lw s2,  64(sp)
    lw s3,  68(sp)
    lw s4,  72(sp)
    lw s5,  76(sp)
    lw s6,  80(sp)
    lw s7,  84(sp)
    lw s8,  88(sp)
    lw s9,  92(sp)
    lw s10, 96(sp)
    lw s11,100(sp)
    lw t3, 104(sp)
    lw t4, 108(sp)
    lw t5, 112(sp)
    lw t6, 116(sp)
    addi sp, sp, 128
    .endm

    .section .exception_vectors.text, "ax", @progbits
    .option push
    .option norvc
    .p2align 2
    .macro VECTOR_INTERRUPT_HANDLER num
    .type __vector_interrupt_\num, @function
__vector_interrupt_\num:
    j interrupt_entry_common
    .size __vector_interrupt_\num, . - __vector_interrupt_\num
    .endm

    VECTOR_INTERRUPT_HANDLER 1
    VECTOR_INTERRUPT_HANDLER 2
    VECTOR_INTERRUPT_HANDLER 3
    VECTOR_INTERRUPT_HANDLER 4
    VECTOR_INTERRUPT_HANDLER 5
    VECTOR_INTERRUPT_HANDLER 6
    VECTOR_INTERRUPT_HANDLER 7
    VECTOR_INTERRUPT_HANDLER 8
    VECTOR_INTERRUPT_HANDLER 9
    VECTOR_INTERRUPT_HANDLER 10
    VECTOR_INTERRUPT_HANDLER 11
    VECTOR_INTERRUPT_HANDLER 12
    VECTOR_INTERRUPT_HANDLER 13
    VECTOR_INTERRUPT_HANDLER 14
    VECTOR_INTERRUPT_HANDLER 15

    .type __trap_entry, @function
__trap_entry:
    SAVE_ALL
    lui t0, 0x60008
    li t1, 0xE001
    addi t0, t0, 0x50
    sw t1, 0(t0)
    csrr a0, mcause
    sw a0, 4(t0)
    csrr t1, mepc
    sw t1, 8(t0)
    csrr t1, mtvec
    sw t1, 12(t0)
    sw sp, 16(t0)
    lui t1, 0x600C2
    lw t2, 0x110(t1)
    sw t2, 20(t0)
    lui t1, 0x600C0
    lw t2, 0x28(t1)
    sw t2, 24(t0)
    lui t1, 0x600C2
    lw t2, 0x104(t1)
    sw t2, 28(t0)
    bltz a0, 1f
    csrr a1, mepc
    csrr a2, mtval
    call trap_exception_c
    j 2f
1:
    call trap_interrupt_c
2:
    RESTORE_ALL
    mret
    .size __trap_entry, . - __trap_entry

    .p2align 2
    .globl interrupt_entry_common
    .type interrupt_entry_common, @function
interrupt_entry_common:
    SAVE_ALL
    csrr a0, mcause
    call trap_interrupt_c
    RESTORE_ALL
    mret
    .size interrupt_entry_common, . - interrupt_entry_common

    .p2align 2
    .globl exception_entry_common
    .type exception_entry_common, @function
exception_entry_common:
    SAVE_ALL
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    call trap_exception_c
    RESTORE_ALL
    mret
    .size exception_entry_common, . - exception_entry_common

    .p2align 2
    .type preempt_return_trampoline, @function
preempt_return_trampoline:
    csrci mstatus, 8
    addi sp, sp, -8
    sw s11, 0(sp)
    la t0, g_trap_preempt_resume_pc
    lw t1, 0(t0)
    sw t1, 4(sp)
    csrsi mstatus, 8
    call process_yield
    lw t0, 4(sp)
    lw s11, 0(sp)
    addi sp, sp, 8
    jalr zero, t0, 0
    .size preempt_return_trampoline, . - preempt_return_trampoline
    .option pop
