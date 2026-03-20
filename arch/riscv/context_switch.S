    .section .text.context_switch, "ax", @progbits
    .globl context_switch
    .globl task_start_first
    .globl task_bootstrap_first_entry_asm
    .globl task_bootstrap_entry_asm
    .globl task_bootstrap_asm

.equ TASK_CONTEXT_SIZE, 52
context_switch:
    addi sp, sp, -TASK_CONTEXT_SIZE
    lui t0, 0x60008
    lui t1, 0xF
    addi t0, t0, 0x50
    addi t1, t1, 0x640
    sw t1, 0(t0)
    sw sp, 4(t0)
    sw ra,   0(sp)
    sw s0,   4(sp)
    sw s1,   8(sp)
    sw s2,  12(sp)
    sw s3,  16(sp)
    sw s4,  20(sp)
    sw s5,  24(sp)
    sw s6,  28(sp)
    sw s7,  32(sp)
    sw s8,  36(sp)
    sw s9,  40(sp)
    sw s10, 44(sp)
    sw s11, 48(sp)
    sw sp,   0(a0)
    mv sp,   a1
    lw ra,   0(sp)
    lw s0,   4(sp)
    lw s1,   8(sp)
    lw s2,  12(sp)
    lw s3,  16(sp)
    lw s4,  20(sp)
    lw s5,  24(sp)
    lw s6,  28(sp)
    lw s7,  32(sp)
    lw s8,  36(sp)
    lw s9,  40(sp)
    lw s10, 44(sp)
    lw s11, 48(sp)
    addi sp, sp, TASK_CONTEXT_SIZE
    lui t0, 0x60008
    lui t1, 0xF
    addi t0, t0, 0x50
    addi t1, t1, 0x641
    sw t1, 0(t0)
    sw ra, 8(t0)
    sw sp, 12(t0)
    ret

task_start_first:
    mv sp, a0
    lui t0, 0x60008
    lui t1, 0xF
    addi t0, t0, 0x50
    addi t1, t1, 0x605
    sw t1, 0(t0)
    sw sp, 4(t0)
    lw ra,   0(sp)
    lw s0,   4(sp)
    lw s1,   8(sp)
    lw s2,  12(sp)
    lw s3,  16(sp)
    lw s4,  20(sp)
    lw s5,  24(sp)
    lw s6,  28(sp)
    lw s7,  32(sp)
    lw s8,  36(sp)
    lw s9,  40(sp)
    lw s10, 44(sp)
    lw s11, 48(sp)
    addi sp, sp, TASK_CONTEXT_SIZE
    lui t0, 0x60008
    lui t1, 0xF
    addi t0, t0, 0x50
    addi t1, t1, 0x606
    sw t1, 0(t0)
    sw ra, 8(t0)
    sw sp, 12(t0)
    jalr zero, ra, 0

    .p2align 2
    .type task_bootstrap_first_entry_asm, @function
task_bootstrap_first_entry_asm:
    .option push
    .option norvc
    lui t0, 0x60008
    lui t1, 0xF
    addi t0, t0, 0x50
    addi t1, t1, 0x607
    sw t1, 0(t0)
    call scheduler_task_bootstrap
    lui t0, 0x60008
    lui t1, 0xF
    addi t0, t0, 0x50
    addi t1, t1, 0x608
    sw t1, 0(t0)
    call task_exit
1:
    wfi
    j 1b
    .size task_bootstrap_first_entry_asm, . - task_bootstrap_first_entry_asm
    .option pop

    .p2align 2
    .type task_bootstrap_entry_asm, @function
task_bootstrap_entry_asm:
    j task_bootstrap_first_entry_asm
    .size task_bootstrap_entry_asm, . - task_bootstrap_entry_asm

    .p2align 2
    .type task_bootstrap_asm, @function
task_bootstrap_asm:
    j task_bootstrap_first_entry_asm
    .size task_bootstrap_asm, . - task_bootstrap_asm
