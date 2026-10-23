/*
 * Erro fatal do kernel: registra tudo na serial, desenha a tela de erro e
 * para a CPU. Também mostra a pilha de chamadas seguindo a cadeia de RBP
 * (por isso compilamos com -fno-omit-frame-pointer).
 */
#include "panic.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "sched/task.h"

static panic_screen_fn screen_fn;
static volatile int in_panic;

void panic_set_screen(panic_screen_fn fn) { screen_fn = fn; }

extern char _text_start[], _text_end[];

static int backtrace(uint64_t rbp, char *out, size_t size) {
    int len = 0;
    for (int depth = 0; depth < 12 && rbp; depth++) {
        uint64_t *frame = (uint64_t *)rbp;
        if (rbp < 0xFFFF800000000000ULL || (rbp & 7)) break;
        uint64_t ret = frame[1];
        if (ret < (uint64_t)_text_start || ret >= (uint64_t)_text_end) break;
        len += snprintf(out + len, size - len, "  %p\n", (void *)ret);
        kprintf("  em %p\n", (void *)ret);
        rbp = frame[0];
    }
    return len;
}

static __attribute__((noreturn)) void halt_forever(void) {
    for (;;) { cli(); hlt(); }
}

void panic(const char *fmt, ...) {
    cli();
    char msg[256], details[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    kprintf("\n*** PANIC: %s\n", msg);
    if (in_panic++) halt_forever();

    uint64_t rbp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    int n = snprintf(details, sizeof(details), "%s\n\nTarefa: %s\nPilha de chamadas:\n", msg,
                     task_current() ? task_current()->name : "-");
    backtrace(rbp, details + n, sizeof(details) - n);
    if (screen_fn) screen_fn("O sistema encontrou um erro e precisou parar", details);
    halt_forever();
}

void panic_exception(const char *name, regs_t *r) {
    cli();
    kprintf("\n*** EXCEÇÃO %lu (%s) erro=%lx rip=%p\n", r->int_no, name, r->err_code, (void *)r->rip);
    if (in_panic++) halt_forever();

    char details[1024];
    int n = snprintf(details, sizeof(details),
        "Exceção %lu: %s\nCódigo de erro: 0x%lx\n", r->int_no, name, r->err_code);
    if (r->int_no == 14)
        n += snprintf(details + n, sizeof(details) - n, "Endereço acessado (CR2): %p\n", (void *)read_cr2());
    n += snprintf(details + n, sizeof(details) - n,
        "Tarefa: %s\n\n"
        "RIP=%p  RSP=%p  RBP=%p\n"
        "RAX=%p  RBX=%p  RCX=%p\n"
        "RDX=%p  RSI=%p  RDI=%p\n\n"
        "Pilha de chamadas:\n  %p\n",
        task_current() ? task_current()->name : "-",
        (void *)r->rip, (void *)r->rsp, (void *)r->rbp,
        (void *)r->rax, (void *)r->rbx, (void *)r->rcx,
        (void *)r->rdx, (void *)r->rsi, (void *)r->rdi, (void *)r->rip);
    kprintf("%s", details);
    backtrace(r->rbp, details + n, sizeof(details) - n);
    if (screen_fn) screen_fn("O sistema encontrou um erro e precisou parar", details);
    halt_forever();
}
