/* Tarefas (threads de kernel) e escalonador preemptivo round-robin. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "arch/x86_64/cpu.h"
#include "sched/event.h"

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_BLOCKED,
    TASK_DEAD,
} task_state_t;

#define EVQ_SIZE 128

typedef struct semaphore sem_t;

typedef struct task {
    int id;
    char name[32];
    task_state_t state;

    regs_t *regs;              /* contexto salvo (na pilha de kernel da tarefa) */
    uint8_t *stack;
    uint64_t stack_size;
    uint8_t *fpu;              /* 512 bytes para FXSAVE, alinhado em 16 */

    uint64_t wake_at;          /* para TASK_SLEEPING */
    uint64_t cpu_ticks, cpu_ticks_prev;
    uint32_t cpu_percent;      /* uso de CPU no último segundo */
    uint64_t started_at;
    uint64_t runs;             /* quantas vezes o escalonador a pôs na CPU */
    uint64_t yields, blocks;   /* cessões voluntárias / entradas em TASK_BLOCKED */
    uint64_t finished_at;

    /* Esperas protegidas por irq_save; cada tarefa espera uma coisa por vez. */
    sem_t *waiting_sem;
    struct task *sem_next;
    bool sem_granted;         /* crédito entregue por post, ainda não consumido */
    bool joinable;            /* conserva a tarefa morta até join ou detach */
    struct task *joiner, *join_target;

    /* fila de eventos */
    event_t evq[EVQ_SIZE];
    int evq_head, evq_count;
    bool waiting_event;
    uint32_t timer_interval;
    uint64_t timer_next;
    bool timer_pending;

    uint64_t mem_bytes;        /* heap alocado por esta tarefa (aproximado) */
    int lock_depth;            /* nº de mutexes segurados */
    bool kill_pending;
    bool system;               /* tarefas do sistema não podem ser finalizadas */

    struct task *next;
} task_t;

typedef void (*task_entry_t)(void *arg);

/* cópia dos dados de uma tarefa (seguro de usar mesmo se ela terminar depois) */
typedef struct {
    int id;
    char name[32];
    task_state_t state;
    uint32_t cpu_percent;
    uint64_t cpu_ms, started_at, finished_at, stack_size, mem_bytes, runs;
    uint64_t yields, blocks;
    bool system;
} task_info_t;

/* Contadores do escalonador desde o boot (laboratório "trocas" do Terminal).
 * Uma troca é "involuntária" quando a tarefa que sai ainda podia rodar: acabou
 * a fatia de tempo dela, acordou alguém ou ela só cedeu a vez (yield). */
typedef struct { uint64_t switches, involuntary; } sched_stats_t;
void    sched_stats(sched_stats_t *out);

void    sched_init(void);
task_t *task_create(const char *name, task_entry_t entry, void *arg);
task_t *task_create_joinable(const char *name, task_entry_t entry, void *arg);
/* Um único joiner; sucesso copia as métricas finais e consome o ponteiro.
 * NULL em info descarta as métricas. Falha para alvo inválido, já reclamado,
 * detached, para a própria tarefa ou para um ciclo de joins. Se o joiner
 * morrer, o alvo continua disponível para outro join/detach. */
bool    task_join(task_t *target, task_info_t *info);
/* Desiste de join; o reap automático passa a recolher a tarefa ao terminar.
 * Falha se outro joiner já a reclamou. Também consome o ponteiro do chamador. */
bool    task_detach(task_t *target);
task_t *task_current(void);
task_t *task_find(int id);
int     task_list(task_t **out, int max);   /* ponteiros: só com interrupções desligadas */
int     task_snapshot(task_info_t *out, int max);
void    task_yield(void);
void    task_sleep(uint64_t ms);
void    task_block(void);                  /* dorme até task_wake() */
void    task_wake(task_t *t);
__attribute__((noreturn)) void task_exit(void);
bool    task_kill(task_t *t);
void    task_idle_loop(void);
void    task_account_memory(int task_id, int64_t delta);   /* usado pelo heap */

/* Gancho chamado na saída de uma tarefa (a GUI fecha as janelas dela) */
void sched_set_exit_hook(void (*hook)(task_t *t));

/* Chamados pela interrupção do timer / despacho de interrupções */
void    sched_tick(uint64_t now);
bool    sched_need_resched(void);
void    sched_request_resched(void);
regs_t *sched_switch(regs_t *current);

/* Mutex: quem não consegue o lock cede a CPU até ele ficar livre */
typedef struct { volatile int locked; task_t *owner; } mutex_t;
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

/* Semáforo contável, só em contexto de tarefa. count nunca fica negativo:
 * post entrega diretamente ao primeiro da fila ou aumenta os créditos livres.
 * Inicialize antes de compartilhar e mantenha vivo até terminarem as esperas. */
struct semaphore { uint64_t count; task_t *head, *tail; };
void sem_init(sem_t *s, uint64_t count);
void sem_wait(sem_t *s);
void sem_post(sem_t *s);
