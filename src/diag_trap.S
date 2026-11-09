    .section .text.diag_trap,"ax",@progbits
    .align 2
    .global HardFault_Handler
    .global NMI_Handler
    .type HardFault_Handler,@function
HardFault_Handler:
NMI_Handler:
    csrci mstatus,8
    csrr a0,mcause
    csrr a1,mepc
    csrr a2,mtval
    mv a3,sp
    .option push
    .option norelax
    la gp,__global_pointer$
    .option pop
    la sp,_eusrstack
    tail diag_fault
    .size HardFault_Handler,.-HardFault_Handler
