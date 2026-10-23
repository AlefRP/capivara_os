/*
 * Stubs de interrupção.
 *
 * A CPU empilha SS, RSP, RFLAGS, CS e RIP (e, em algumas exceções, um código
 * de erro). Cada stub uniformiza a pilha (empurra um código de erro falso se
 * preciso + o número do vetor) e pula para isr_common, que salva todos os
 * registradores e chama isr_dispatch(regs_t *).
 *
 * isr_dispatch devolve o regs_t* a restaurar. Se for o de outra tarefa, a
 * pilha é trocada e o iretq "volta" para dentro da outra tarefa: é assim que
 * a troca de contexto acontece.
 */
    .section .text

.macro STUB_NOERR n
    .global isr_stub_\n
isr_stub_\n:
    pushq $0
    pushq $\n
    jmp isr_common
.endm

.macro STUB_ERR n
    .global isr_stub_\n
isr_stub_\n:
    pushq $\n
    jmp isr_common
.endm

/* Exceções que já empilham código de erro: 8, 10-14, 17, 21, 29, 30 */
.irp n, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31
    STUB_NOERR \n
.endr
.irp n, 8,10,11,12,13,14,17,21,29,30
    STUB_ERR \n
.endr
/* IRQs 0-15 (vetores 32-47), yield (0x81 = 129) e chamada de sistema (0x80) */
.irp n, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,128,129
    STUB_NOERR \n
.endr

isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    mov %rsp, %rdi              /* regs_t* atual */
    call isr_dispatch
    mov %rax, %rsp              /* regs_t* a restaurar (talvez de outra tarefa) */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    add $16, %rsp               /* descarta número do vetor e código de erro */
    iretq

/* Tabela com o endereço de cada stub, usada para montar a IDT */
    .section .rodata
    .align 8
    .global isr_stub_table
isr_stub_table:
.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .quad isr_stub_\n
.endr
    .global isr_stub_syscall, isr_stub_yield
isr_stub_syscall: .quad isr_stub_128
isr_stub_yield:   .quad isr_stub_129
