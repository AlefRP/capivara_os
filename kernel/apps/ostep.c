/*
 * Laboratórios do OSTEP ("Operating Systems: Three Easy Pieces", de Remzi e
 * Andrea Arpaci-Dusseau, grátis em pages.cs.wisc.edu/~remzi/OSTEP). Cada
 * comando do Terminal mostra um assunto do livro:
 *
 *   no sistema de verdade:  trocas (troca de contexto), traduzir (tabelas de
 *                           página), heap (espaço livre), corrida (locks),
 *                           stat (arquivos), prodcons (sincronização)
 *   em simuladores:         escalonar (políticas de CPU), paginas
 *                           (substituição de páginas), disco (braço do disco)
 *
 * Os simuladores existem porque o Capivara não tem essas peças (escalonador
 * com prioridades, memória virtual em disco, um disco): eles fazem as contas
 * dos capítulos passo a passo, para conferir com as figuras do livro.
 *
 * O guia que liga cada capítulo a estes comandos e ao código está em
 * /home/usuario/Documentos/OSTEP (app Estudos) e em docs/ostep.md.
 */
#include "apps/ostep.h"
#include "sched/task.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "fs/vfs.h"
#include "dev/rtc.h"
#include "arch/x86_64/pit.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "panic.h"

#define ABS_DIFF(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))

static lab_out_t *out;

static void say(int color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(int color, const char *fmt, ...) {
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out->write(out->ctx, color, buf);
}
#define txt(...) say(LAB_TEXT, __VA_ARGS__)

/* número com uma casa decimal, a partir de dez vezes o valor ("12,5") */
static const char *dec1(char *buf, long x10) {
    snprintf(buf, 16, "%ld,%ld", x10 / 10, x10 % 10);
    return buf;
}

static bool num(const char *s, long *v) {
    const char *end;
    long x = strtol(s, &end, 10);
    if (end == s || *end) return false;
    *v = x;
    return true;
}

/* "chave=valor": devolve o valor se a chave bater */
static bool option(const char *arg, const char *key, long *v) {
    size_t n = strlen(key);
    return !strncmp(arg, key, n) && arg[n] == '=' && num(arg + n + 1, v);
}

/* ================================================================== */
/* escalonar: políticas de CPU (caps. 7, 8 e 9)                        */
/* ================================================================== */
#define MAXJOBS 8
#define MAXT    240
#define LEVELS  3

typedef struct {
    int arrival, length, tickets;
    int left, first, finish;       /* first = -1 até rodar pela primeira vez */
    int level, used;               /* MLFQ: fila atual e tempo gasto nela */
} job_t;

enum { P_FIFO, P_SJF, P_STCF, P_RR, P_MLFQ, P_LOT };
static const char *const pol_name[] = { "fifo", "sjf", "stcf", "rr", "mlfq", "loteria" };
static const char *const pol_desc[] = {
    "o primeiro que chega roda até terminar",
    "o mais curto roda primeiro, sem ser interrompido",
    "sempre o que tem menos tempo restante (interrompe quem está rodando)",
    "cada um roda um quantum e volta para o fim da fila",
    "3 filas de prioridade: quem gasta a fatia desce; boost a cada s unidades",
    "a cada unidade de tempo, sorteia um bilhete entre quem está pronto",
};
static const int job_color[MAXJOBS] = {
    LAB_CYAN, LAB_YELLOW, LAB_GREEN, LAB_MAGENTA, LAB_BLUE, LAB_RED, LAB_WHITE, LAB_CYAN,
};

/* fila circular de índices de tarefas */
typedef struct { int v[64], head, n; } queue_t;
static void q_push(queue_t *q, int j) { if (q->n < 64) q->v[(q->head + q->n++) % 64] = j; }
static int  q_pop(queue_t *q) { int j = q->v[q->head]; q->head = (q->head + 1) % 64; q->n--; return j; }

static uint32_t rng = 1;
static uint32_t xorshift(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* escolhe, entre as prontas, a de menor chave (chegada, duração ou restante) */
static int pick_min(job_t *jobs, int n, int t, int pol) {
    int best = -1;
    for (int j = 0; j < n; j++) {
        if (jobs[j].arrival > t || !jobs[j].left) continue;
        int k = pol == P_FIFO ? jobs[j].arrival : pol == P_SJF ? jobs[j].length : jobs[j].left;
        int kb = best < 0 ? 0 : pol == P_FIFO ? jobs[best].arrival : pol == P_SJF ? jobs[best].length : jobs[best].left;
        if (best < 0 || k < kb) best = j;
    }
    return best;
}

static void simulate(job_t *jobs, int n, int pol, int quantum, int boost, signed char *tl, int *tend) {
    queue_t rq = { 0 }, mq[LEVELS] = { 0 };
    int cur = -1, slice = 0, done = 0, t = 0;
    for (; t < MAXT && done < n; t++) {
        /* chegadas */
        for (int j = 0; j < n; j++)
            if (jobs[j].arrival == t) {
                if (pol == P_RR) q_push(&rq, j);
                if (pol == P_MLFQ) q_push(&mq[0], j);       /* regra 3: entra na fila do topo */
            }
        if (cur >= 0 && !jobs[cur].left) cur = -1;

        switch (pol) {
        case P_FIFO: case P_SJF:
            if (cur < 0) cur = pick_min(jobs, n, t, pol);
            break;
        case P_STCF:
            cur = pick_min(jobs, n, t, pol);
            break;
        case P_RR:
            if (cur >= 0 && slice >= quantum) { q_push(&rq, cur); cur = -1; }
            if (cur < 0 && rq.n) { cur = q_pop(&rq); slice = 0; }
            break;
        case P_MLFQ: {
            if (boost && t > 0 && t % boost == 0) {
                /* regra 5: de tempos em tempos, todo mundo volta para o topo */
                if (cur >= 0) { q_push(&mq[jobs[cur].level], cur); cur = -1; }
                for (int l = 1; l < LEVELS; l++)
                    while (mq[l].n) q_push(&mq[0], q_pop(&mq[l]));
                for (int j = 0; j < n; j++) { jobs[j].level = 0; jobs[j].used = 0; }
            }
            if (cur >= 0) {
                job_t *c = &jobs[cur];
                int top = 0;
                while (top < LEVELS && !mq[top].n) top++;
                if (c->used >= quantum << c->level) {
                    /* regra 4: gastou a cota da fila, desce um nível */
                    if (c->level < LEVELS - 1) c->level++;
                    c->used = 0;
                    q_push(&mq[c->level], cur);
                    cur = -1;
                } else if (top < c->level) {
                    q_push(&mq[c->level], cur);    /* regra 1: alguém mais prioritário chegou */
                    cur = -1;
                }
            }
            if (cur < 0)
                for (int l = 0; l < LEVELS; l++)
                    if (mq[l].n) { cur = q_pop(&mq[l]); break; }
            break;
        }
        case P_LOT: {
            int total = 0;
            for (int j = 0; j < n; j++)
                if (jobs[j].arrival <= t && jobs[j].left) total += jobs[j].tickets;
            cur = -1;
            if (total) {
                int r = (int)(xorshift() % (uint32_t)total);
                for (int j = 0; j < n; j++) {
                    if (jobs[j].arrival > t || !jobs[j].left) continue;
                    if (r < jobs[j].tickets) { cur = j; break; }
                    r -= jobs[j].tickets;
                }
            }
            break;
        }
        }

        tl[t] = (signed char)cur;
        if (cur < 0) continue;
        job_t *c = &jobs[cur];
        if (c->first < 0) c->first = t;
        c->left--;
        c->used++;
        slice++;
        if (!c->left) { c->finish = t + 1; done++; }
    }
    *tend = t;
}

static void print_timeline(const signed char *tl, int tend) {
    for (int base = 0; base < tend; base += 50) {
        int w = tend - base < 50 ? tend - base : 50;
        char scale[64];
        memset(scale, ' ', sizeof(scale));
        for (int i = 0; i < w; i += 10) {
            char nb[8];
            int k = snprintf(nb, sizeof(nb), "%d", base + i);
            memcpy(scale + i, nb, (size_t)k);
        }
        scale[w + 4 < 63 ? w + 4 : 63] = 0;
        say(LAB_DIM, "  %s\n  ", scale);
        for (int i = 0; i < w; i++) say(LAB_DIM, "%c", i % 10 ? '-' : '|');
        txt("\n  ");
        for (int i = 0; i < w; i++) {
            int j = tl[base + i];
            if (j < 0) say(LAB_DIM, ".");
            else say(job_color[j], "%c", 'A' + j);
        }
        txt("\n");
    }
}

static void lab_escalonar(int argc, char **argv) {
    int pol = -1;
    if (argc > 1)
        for (int p = 0; p <= P_LOT; p++)
            if (!strcmp(argv[1], pol_name[p])) pol = p;
    if (pol < 0) {
        txt("uso: escalonar <fifo|sjf|stcf|rr|mlfq|loteria> [q=N] [s=N] [semente=N] [tarefas]\n");
        txt("     cada tarefa é duração, chegada:duração ou chegada:duração:bilhetes\n");
        say(LAB_DIM, "ex.: escalonar fifo 0:100 0:10 0:10      (o \"efeito comboio\", cap. 7)\n");
        say(LAB_DIM, "     escalonar rr q=1 0:5 0:5 0:5        (tempo de resposta, cap. 7)\n");
        say(LAB_DIM, "     escalonar mlfq q=2 s=40 0:60 10:6   (filas e boost, cap. 8)\n");
        say(LAB_DIM, "     escalonar loteria 0:40:75 0:40:25   (bilhetes, cap. 9)\n");
        return;
    }
    long quantum = 2, boost = 0, seed = 1;
    job_t jobs[MAXJOBS];
    int n = 0, total = 0;
    for (int i = 2; i < argc; i++) {
        if (option(argv[i], "q", &quantum) || option(argv[i], "s", &boost) || option(argv[i], "semente", &seed)) continue;
        long v[3] = { 0, 0, 100 };
        int k = 0;
        char tmp[32];
        strlcpy(tmp, argv[i], sizeof(tmp));
        for (char *p = tmp, *s = tmp; k < 3; p++) {
            if (*p != ':' && *p) continue;
            bool last = !*p;
            *p = 0;
            if (!num(s, &v[k++])) { k = -1; break; }
            if (last) break;
            s = p + 1;
        }
        if (k < 1) { say(LAB_RED, "tarefa inválida: '%s'\n", argv[i]); return; }
        if (k == 1) { v[1] = v[0]; v[0] = 0; }           /* só a duração: chega no tempo 0 */
        if (n == MAXJOBS) { say(LAB_RED, "no máximo %d tarefas\n", MAXJOBS); return; }
        if (v[0] < 0 || v[0] > 100 || v[1] < 1 || v[1] > 120 || v[2] < 1 || v[2] > 1000) {
            say(LAB_RED, "'%s': chegada de 0 a 100, duração de 1 a 120, bilhetes de 1 a 1000\n", argv[i]);
            return;
        }
        jobs[n++] = (job_t){ .arrival = (int)v[0], .length = (int)v[1], .tickets = (int)v[2] };
        total += (int)v[1];
    }
    if (!n) {   /* sem tarefas: um exemplo pequeno, com chegadas diferentes */
        jobs[0] = (job_t){ .arrival = 0, .length = 12, .tickets = 100 };
        jobs[1] = (job_t){ .arrival = 1, .length = 4, .tickets = 100 };
        jobs[2] = (job_t){ .arrival = 2, .length = 6, .tickets = 100 };
        n = 3;
        total = 22;
    }
    if (quantum < 1 || quantum > 50) quantum = 2;
    if (total > MAXT - 100) { say(LAB_RED, "tempo total grande demais (máximo %d)\n", MAXT - 100); return; }
    for (int j = 0; j < n; j++) {
        jobs[j].left = jobs[j].length;
        jobs[j].first = -1;
        jobs[j].finish = 0;
    }
    rng = seed ? (uint32_t)seed : 1;

    say(LAB_YELLOW, "Política %s: ", pol_name[pol]);
    txt("%s.\n", pol_desc[pol]);
    if (pol == P_RR) say(LAB_DIM, "quantum = %ld\n", quantum);
    if (pol == P_MLFQ) say(LAB_DIM, "quantum = %ld, %ld e %ld nas filas 0, 1 e 2; boost %s%ld\n",
                           quantum, quantum * 2, quantum * 4, boost ? "a cada " : "desligado", boost ? boost : 0);
    for (int j = 0; j < n; j++) {
        say(job_color[j], "  %c", 'A' + j);
        txt(": chega em %d, precisa de %d", jobs[j].arrival, jobs[j].length);
        if (pol == P_LOT) txt(", %d bilhetes", jobs[j].tickets);
        txt("\n");
    }
    signed char tl[MAXT];
    int tend;
    simulate(jobs, n, pol, (int)quantum, (int)boost, tl, &tend);
    print_timeline(tl, tend);

    /* retorno = fim - chegada; resposta = 1ª vez na CPU - chegada; espera = retorno - duração */
    say(LAB_DIM, "tarefa  chegada  duração   começo      fim  retorno resposta   espera\n");
    long sr = 0, sp = 0, sw = 0;
    for (int j = 0; j < n; j++) {
        job_t *c = &jobs[j];
        int ret = c->finish - c->arrival, resp = c->first - c->arrival, wait = ret - c->length;
        say(job_color[j], "  %c   ", 'A' + j);
        txt("%9d%9d%9d%9d%9d%9d%9d\n", c->arrival, c->length, c->first, c->finish, ret, resp, wait);
        sr += ret;
        sp += resp;
        sw += wait;
    }
    char a[16], b[16], c[16];
    say(LAB_GREEN, "média %36s%9s%9s%9s\n", "", dec1(a, sr * 10 / n), dec1(b, sp * 10 / n), dec1(c, sw * 10 / n));
}

/* ================================================================== */
/* paginas: substituição de páginas (cap. 22)                          */
/* ================================================================== */
#define MAXFRAMES 8
#define MAXREFS   40

enum { R_FIFO, R_LRU, R_OPT, R_CLOCK };
static const char *const rep_name[] = { "fifo", "lru", "opt", "relogio" };

/* devolve o número de faltas; com "show", imprime passo a passo */
static int replace_sim(int pol, int frames, const int *refs, int nrefs, bool show) {
    int page[MAXFRAMES], stamp[MAXFRAMES], use[MAXFRAMES], used = 0, hand = 0, misses = 0;
    bool seen[1000] = { 0 };
    for (int i = 0; i < nrefs; i++) {
        int p = refs[i], slot = -1, evicted = -1;
        for (int f = 0; f < used; f++)
            if (page[f] == p) slot = f;
        bool hit = slot >= 0;
        if (hit) {
            if (pol == R_LRU) stamp[slot] = i;
            use[slot] = 1;
        } else {
            misses++;
            if (used < frames) {
                slot = used++;                              /* ainda há quadro vazio */
            } else {
                if (pol == R_FIFO || pol == R_LRU) {
                    slot = 0;                               /* o mais antigo: carga (FIFO) ou uso (LRU) */
                    for (int f = 1; f < used; f++)
                        if (stamp[f] < stamp[slot]) slot = f;
                } else if (pol == R_OPT) {
                    int far = -1;                           /* a que vai ser usada mais tarde (ou nunca) */
                    for (int f = 0; f < used; f++) {
                        int next = nrefs;
                        for (int k = i + 1; k < nrefs; k++)
                            if (refs[k] == page[f]) { next = k; break; }
                        if (next > far) { far = next; slot = f; }
                    }
                } else {
                    /* relógio: quem tem o bit de uso ligado ganha uma segunda chance */
                    while (use[hand]) { use[hand] = 0; hand = (hand + 1) % used; }
                    slot = hand;
                    hand = (hand + 1) % used;
                }
                evicted = page[slot];
            }
            page[slot] = p;
            stamp[slot] = i;
            use[slot] = 1;
        }
        if (!show) {
            seen[p] = true;
            continue;
        }
        say(LAB_TEXT, "  %3d  ", p);
        if (hit) say(LAB_GREEN, "acerto       ");
        else if (!seen[p]) say(LAB_YELLOW, "falta (1ª)   ");
        else say(LAB_RED, "falta        ");
        seen[p] = true;
        txt("[");
        for (int f = 0; f < frames; f++) {
            if (f < used) say(f == slot ? LAB_WHITE : LAB_TEXT, "%3d", page[f]);
            else say(LAB_DIM, "  .");
        }
        txt(" ]");
        if (evicted >= 0 && !hit) say(LAB_DIM, "  sai a %d", evicted);
        if (pol == R_CLOCK) say(LAB_DIM, "  ponteiro em %d", hand);
        txt("\n");
    }
    return misses;
}

static void lab_paginas(int argc, char **argv) {
    static const int belady[] = { 1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5 };
    if (argc > 1 && !strcmp(argv[1], "belady")) {
        txt("Anomalia de Belady: com FIFO, mais memória pode dar MAIS faltas.\n");
        say(LAB_DIM, "referências: 1 2 3 4 1 2 5 1 2 3 4 5\n");
        for (int f = 3; f <= 4; f++)
            txt("  FIFO com %d quadros: %d faltas\n", f, replace_sim(R_FIFO, f, belady, 12, false));
        for (int f = 3; f <= 4; f++)
            txt("  LRU  com %d quadros: %d faltas\n", f, replace_sim(R_LRU, f, belady, 12, false));
        say(LAB_DIM, "O LRU (como o OPT) é uma política \"de pilha\": não sofre a anomalia.\n");
        return;
    }
    int pol = -1;
    if (argc > 1)
        for (int p = 0; p <= R_CLOCK; p++)
            if (!strcmp(argv[1], rep_name[p])) pol = p;
    long frames = 3;
    if (pol < 0 || (argc > 2 && (!num(argv[2], &frames) || frames < 1 || frames > MAXFRAMES))) {
        txt("uso: paginas <fifo|lru|opt|relogio> [quadros] [referências...]\n");
        txt("     paginas belady\n");
        say(LAB_DIM, "ex.: paginas lru 3 0 1 2 0 1 3 0 3 1 2 1    (a sequência dos exemplos do cap. 22)\n");
        return;
    }
    int refs[MAXREFS], n = 0;
    for (int i = 3; i < argc && n < MAXREFS; i++) {
        long v;
        if (!num(argv[i], &v) || v < 0 || v > 999) { say(LAB_RED, "página inválida: '%s' (use 0 a 999)\n", argv[i]); return; }
        refs[n++] = (int)v;
    }
    if (!n) {
        static const int ex[] = { 0, 1, 2, 0, 1, 3, 0, 3, 1, 2, 1 };
        memcpy(refs, ex, sizeof(ex));
        n = 11;
    }
    say(LAB_YELLOW, "Política %s com %ld quadros\n", rep_name[pol], frames);
    say(LAB_DIM, "  pág  resultado    quadros\n");
    int misses = replace_sim(pol, (int)frames, refs, n, true);
    char a[16];
    say(LAB_GREEN, "acertos %d, faltas %d, taxa de acerto %s%%\n", n - misses, misses, dec1(a, (long)(n - misses) * 1000 / n));
}

/* ================================================================== */
/* disco: escalonamento do braço do disco (cap. 37)                    */
/* ================================================================== */
enum { D_FIFO, D_SSTF, D_SCAN, D_CSCAN };
static const char *const disk_name[] = { "fifo", "sstf", "scan", "cscan" };

static void lab_disco(int argc, char **argv) {
    int pol = -1;
    if (argc > 1)
        for (int p = 0; p <= D_CSCAN; p++)
            if (!strcmp(argv[1], disk_name[p])) pol = p;
    long head = 50;
    if (pol < 0 || (argc > 2 && (!num(argv[2], &head) || head < 0 || head > 199))) {
        txt("uso: disco <fifo|sstf|scan|cscan> [cabeça] [trilhas...]   (trilhas de 0 a 199)\n");
        say(LAB_DIM, "ex.: disco sstf 50 95 180 34 119 11 123 62 64\n");
        return;
    }
    int req[32], n = 0;
    for (int i = 3; i < argc && n < 32; i++) {
        long v;
        if (!num(argv[i], &v) || v < 0 || v > 199) { say(LAB_RED, "trilha inválida: '%s'\n", argv[i]); return; }
        req[n++] = (int)v;
    }
    if (!n) {
        static const int ex[] = { 95, 180, 34, 119, 11, 123, 62, 64 };
        memcpy(req, ex, sizeof(ex));
        n = 8;
    }
    bool done[32] = { 0 };
    int order[32], pos = (int)head, total = 0;
    for (int k = 0; k < n; k++) {
        int best = -1;
        if (pol == D_FIFO) {
            best = k;
        } else if (pol == D_SSTF) {
            for (int i = 0; i < n; i++)
                if (!done[i] && (best < 0 || ABS_DIFF(req[i], pos) < ABS_DIFF(req[best], pos))) best = i;
        } else {
            /* o elevador sobe atendendo quem está no caminho; o SCAN depois desce,
             * o C-SCAN volta para o começo e sobe de novo */
            for (int i = 0; i < n; i++)
                if (!done[i] && req[i] >= pos && (best < 0 || req[i] < req[best])) best = i;
            if (best < 0)
                for (int i = 0; i < n; i++) {
                    if (done[i]) continue;
                    if (pol == D_SCAN && (best < 0 || req[i] > req[best])) best = i;
                    if (pol == D_CSCAN && (best < 0 || req[i] < req[best])) best = i;
                }
        }
        done[best] = true;
        order[k] = best;
        total += ABS_DIFF(req[best], pos);
        pos = req[best];
    }
    say(LAB_YELLOW, "Política %s, cabeça começa na trilha %ld\n", disk_name[pol], head);
    pos = (int)head;
    for (int k = 0; k < n; k++) {
        int r = req[order[k]];
        char bar[52];
        memset(bar, '.', 50);
        bar[50] = 0;
        bar[r / 4] = '#';
        say(LAB_DIM, "  %3d -> %3d  ", pos, r);
        txt("%3d  ", ABS_DIFF(r, pos));
        say(LAB_CYAN, "%s\n", bar);
        pos = r;
    }
    char a[16];
    say(LAB_GREEN, "deslocamento total: %d trilhas (média %s por pedido)\n", total, dec1(a, (long)total * 10 / n));
    if (pol == D_CSCAN) say(LAB_DIM, "(a volta para o começo também conta)\n");
}

/* ================================================================== */
/* traduzir: o caminho de um endereço pelas tabelas de página (18-20)  */
/* ================================================================== */
static bool hex64(const char *s, uint64_t *v) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint64_t x = 0;
    int digits = 0;
    for (; *s; s++) {
        if (*s == '_' || *s == '\'') continue;          /* separadores: 0xffff_8000_0000_0000 */
        int c = tolower(*s), d = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (d < 0 || ++digits > 16) return false;
        x = x << 4 | (uint64_t)d;
    }
    *v = x;
    return digits > 0;
}

static void flags(uint64_t e, bool huge_level) {
    static const struct { uint64_t bit; const char *name; } f[] = {
        { PTE_PRESENT, "P" }, { PTE_WRITE, "W" }, { PTE_USER, "U" }, { PTE_PWT, "PWT" }, { PTE_PCD, "PCD" },
        { 1 << 5, "A" }, { 1 << 6, "D" }, { PTE_HUGE, "PS" }, { PTE_GLOBAL, "G" }, { 1ULL << 63, "NX" },
    };
    for (unsigned i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
        if (f[i].bit == PTE_HUGE && !huge_level) continue;    /* na PT o bit 7 é outra coisa (PAT) */
        if (e & f[i].bit) say(LAB_CYAN, " %s", f[i].name);
    }
}

static void walk_one(uint64_t va, const char *what) {
    static const char *const lvl[] = { "PML4", "PDPT", "PD  ", "PT  " };
    vmm_walk_t w;
    bool ok = vmm_walk(va, &w);
    say(LAB_YELLOW, "0x%016lx", va);
    if (what) say(LAB_DIM, "  (%s)", what);
    txt("\n");
    if (!w.levels) {
        say(LAB_RED, "  endereço não canônico: os bits 63..48 precisam repetir o bit 47\n");
        return;
    }
    say(LAB_DIM, "  índices: PML4 %u, PDPT %u, PD %u, PT %u; deslocamento 0x%lx\n",
        (unsigned)((va >> 39) & 511), (unsigned)((va >> 30) & 511), (unsigned)((va >> 21) & 511),
        (unsigned)((va >> 12) & 511), va & 0xFFF);
    say(LAB_DIM, "  CR3 = 0x%016lx (endereço físico da PML4)\n", w.cr3);
    for (int l = 0; l < w.levels; l++) {
        txt("  %s[%3u] = 0x%016lx", lvl[l], (unsigned)w.index[l], w.entry[l]);
        flags(w.entry[l], l == 1 || l == 2);
        if (!(w.entry[l] & PTE_PRESENT)) say(LAB_RED, "  ausente: aqui daria page fault");
        txt("\n");
    }
    if (ok) {
        const char *sz = w.page_size == 4096 ? "4 KiB" : w.page_size == (2ULL << 20) ? "2 MiB" : "1 GiB";
        say(LAB_GREEN, "  -> página de %s, endereço físico 0x%016lx\n", sz, w.phys);
        if (w.page_size > 4096)
            say(LAB_DIM, "     página grande: a tabela de baixo não existe, e os %d bits de baixo (0x%lx) são o deslocamento\n",
                w.page_size == (2ULL << 20) ? 21 : 30, va & (w.page_size - 1));
    }
}

static void lab_traduzir(int argc, char **argv) {
    if (argc > 1) {
        uint64_t va;
        if (!hex64(argv[1], &va)) { say(LAB_RED, "uso: traduzir [endereço em hexadecimal, ex.: 0xffffffff80100000]\n"); return; }
        walk_one(va, NULL);
        return;
    }
    txt("Como a MMU traduz cada endereço: 9 bits por nível, de cima para baixo.\n");
    say(LAB_DIM, "Bits: P presente, W escrita, U usuário, A acessada, D suja, PS página grande, G global, NX sem execução\n\n");
    int local = 0;
    void *p = kmalloc(64);
    walk_one((uint64_t)&lab_run, "código do kernel: a função lab_run");
    walk_one((uint64_t)p, "um bloco do heap, acabou de sair do kmalloc");
    walk_one((uint64_t)&local, "uma variável na pilha desta tarefa (a pilha também é do heap)");
    walk_one((uint64_t)phys_to_virt(0x100000), "physmap: o físico 1 MiB visto em PHYSMAP_BASE + 1 MiB");
    walk_one(0, "o ponteiro nulo: o mapeamento do boot foi apagado de propósito");
    kfree(p);
}

/* ================================================================== */
/* heap: gerência de espaço livre (cap. 17)                            */
/* ================================================================== */
#define SNAP_MAX 2048
static heap_block_t snap[SNAP_MAX];

static const char *task_name(int id, char *buf, size_t size) {
    static task_info_t tl[64];
    int n = task_snapshot(tl, 64);
    for (int i = 0; i < n; i++)
        if (tl[i].id == id) { strlcpy(buf, tl[i].name, size); return buf; }
    if (id < 0) strlcpy(buf, "boot", size);
    else snprintf(buf, size, "tarefa %d (já terminou)", id);
    return buf;
}

static void show_range(uint64_t from, uint64_t to) {
    int n = heap_snapshot(snap, SNAP_MAX);
    if (n > SNAP_MAX) n = SNAP_MAX;
    for (int i = 0; i < n; i++) {
        if (snap[i].addr + snap[i].size <= from || snap[i].addr >= to) continue;
        say(snap[i].free ? LAB_GREEN : LAB_YELLOW, "    [%s %5lu bytes]", snap[i].free ? "livre" : "usado", snap[i].size);
        say(LAB_DIM, " em +0x%lx\n", (unsigned long)(snap[i].addr - HEAP_BASE));
    }
}

static void lab_heap(int argc, char **argv) {
    heap_stats_t hs;
    heap_stats(&hs);
    int n = heap_snapshot(snap, SNAP_MAX);
    if (n > SNAP_MAX) n = SNAP_MAX;
    uint64_t free_total = 0, largest = 0;
    int nfree = 0;
    for (int i = 0; i < n; i++)
        if (snap[i].free) {
            nfree++;
            free_total += snap[i].size;
            if (snap[i].size > largest) largest = snap[i].size;
        }

    if (argc > 1 && !strcmp(argv[1], "blocos")) {
        long max = 30;
        if (argc > 2) num(argv[2], &max);
        say(LAB_DIM, "  endereço (desde HEAP_BASE)   tamanho  estado  dono\n");
        char nm[48];
        for (int i = 0; i < n && i < max; i++) {
            txt("  +0x%08lx  %15lu  ", (unsigned long)(snap[i].addr - HEAP_BASE), snap[i].size);
            say(snap[i].free ? LAB_GREEN : LAB_YELLOW, "%s", snap[i].free ? "livre " : "usado ");
            if (!snap[i].free) say(LAB_DIM, "  %s", task_name(snap[i].owner, nm, sizeof(nm)));
            txt("\n");
        }
        if (n > max) say(LAB_DIM, "  ... mais %ld blocos (heap blocos %d mostra todos)\n", n - max, n);
        return;
    }

    if (argc > 1 && !strcmp(argv[1], "teste")) {
        txt("Divisão e junção de blocos, ao vivo no heap do kernel:\n");
        char *a = kmalloc(100), *b = kmalloc(200), *c = kmalloc(300);
        if (!a || !b || !c) { kfree(a); kfree(b); kfree(c); return; }
        uint64_t lo = (uint64_t)a < (uint64_t)c ? (uint64_t)a : (uint64_t)c, hi = (uint64_t)a > (uint64_t)c ? (uint64_t)a : (uint64_t)c;
        if ((uint64_t)b < lo) lo = (uint64_t)b;
        if ((uint64_t)b > hi) hi = (uint64_t)b;
        lo -= 32;            /* o cabeçalho (block_t) fica logo antes dos dados */
        hi += 336;
        say(LAB_CYAN, "1. kmalloc(100), kmalloc(200), kmalloc(300):\n");
        say(LAB_DIM, "   cada bloco tem 32 bytes de cabeçalho e o tamanho sobe para múltiplo de 16\n");
        show_range(lo, hi);
        kfree(b);
        say(LAB_CYAN, "2. kfree(o de 200): vira um buraco livre no meio\n");
        show_range(lo, hi);
        kfree(a);
        say(LAB_CYAN, "3. kfree(o de 100): os dois livres vizinhos se juntam num só\n");
        show_range(lo, hi);
        kfree(c);
        say(LAB_CYAN, "4. kfree(o de 300): junta de novo, com o que vier depois\n");
        show_range(lo, hi + 4096);
        return;
    }

    say(LAB_YELLOW, "Heap do kernel (first fit, com junção de vizinhos)\n");
    txt("  tamanho mapeado: %lu KiB em 0x%lx\n", hs.size >> 10, (unsigned long)HEAP_BASE);
    txt("  em uso: %lu KiB; livre: %lu KiB\n", hs.used >> 10, free_total >> 10);
    txt("  blocos: %d (%d livres); maior bloco livre: %lu KiB\n", n, nfree, largest >> 10);
    char a[16];
    if (free_total)
        txt("  fragmentação externa: %s%%  (1 - maior livre / total livre)\n",
            dec1(a, (long)(1000 - largest * 1000 / free_total)));
    txt("  memória física: %lu MiB livres de %lu MiB (bitmap, 1 bit por página de 4 KiB)\n",
        pmm_free_bytes() >> 20, pmm_total_bytes() >> 20);
    say(LAB_DIM, "Veja também: heap blocos [N], heap teste\n");
}

/* ================================================================== */
/* trocas: troca de contexto de verdade (caps. 6 e 7)                  */
/* ================================================================== */
static const char *state_str(task_state_t s) {
    switch (s) {
    case TASK_READY: return "pronta";
    case TASK_RUNNING: return "rodando";
    case TASK_SLEEPING: return "dormindo";
    case TASK_BLOCKED: return "esperando";
    default: return "morta";
    }
}

static void lab_trocas(void) {
    sched_stats_t s0, s1;
    sched_stats(&s0);
    say(LAB_DIM, "medindo por 1 segundo...\n");
    task_sleep(1000);
    sched_stats(&s1);
    say(LAB_YELLOW, "Escalonador round-robin, fatia de 10 ms (o PIT interrompe 1000 vezes por segundo)\n");
    txt("  trocas de contexto desde o boot: %lu (%lu involuntárias)\n", s1.switches, s1.involuntary);
    txt("  no último segundo: %lu trocas, %lu involuntárias\n", s1.switches - s0.switches, s1.involuntary - s0.involuntary);
    static task_info_t tl[64];
    int n = task_snapshot(tl, 64);
    say(LAB_DIM, "   id  tarefa                    estado      CPU (ms)  vezes na CPU\n");
    for (int i = 0; i < n; i++)
        txt("  %3d  %-24.24s  %-9s %10lu  %12lu\n", tl[i].id, tl[i].name, state_str(tl[i].state), tl[i].cpu_ms, tl[i].runs);
    say(LAB_DIM, "Quase todas dormem ou esperam eventos: a CPU fica na tarefa \"ocioso\" (hlt).\n");
}

/* ================================================================== */
/* corrida: condição de corrida e lock (caps. 26 a 28)                 */
/* ================================================================== */
static struct {
    volatile long counter;
    mutex_t lock;
    long n;
    bool use_lock;
    volatile int done;
    bool running;
} race;

/* Cada tarefa soma 1 no contador, n vezes. Sem o lock, "ler" e "escrever"
 * são dois passos: se a troca de contexto cair entre eles, a outra tarefa
 * soma milhares de vezes e depois esta escreve o valor velho por cima. */
static void race_worker(void *arg) {
    (void)arg;
    for (long i = 0; i < race.n; i++) {
        if (race.use_lock) mutex_lock(&race.lock);
        long v = race.counter;
        race.counter = v + 1;
        if (race.use_lock) mutex_unlock(&race.lock);
    }
    __atomic_add_fetch(&race.done, 1, __ATOMIC_SEQ_CST);
}

static void lab_corrida(int argc, char **argv) {
    if (race.running && race.done < 2) { say(LAB_RED, "a corrida anterior ainda está rodando\n"); return; }
    /* Precisa durar várias fatias de 10 ms, senão cada tarefa termina antes de
     * ser interrompida e a corrida não aparece. */
    bool lock = false;
    long n = 50000000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "trava")) lock = true;
        else if (!num(argv[i], &n) || n < 1000 || n > 500000000) {
            txt("uso: corrida [trava] [N]   (duas tarefas somam 1 num contador, N vezes cada)\n");
            return;
        }
    }
    race.counter = 0;
    race.n = n;
    race.use_lock = lock;
    race.done = 0;
    race.running = true;
    say(LAB_DIM, "duas tarefas, %ld somas cada, %s...\n", n, lock ? "com mutex" : "sem lock");
    uint64_t t0 = timer_ms();
    task_create("corrida A", race_worker, NULL);
    task_create("corrida B", race_worker, NULL);
    while (race.done < 2 && timer_ms() - t0 < 120000) task_sleep(5);
    if (race.done < 2) { say(LAB_RED, "passou de 2 minutos; elas continuam rodando\n"); return; }
    race.running = false;
    long got = race.counter, want = 2 * n;
    txt("  esperado: %ld\n  obtido:   %ld\n", want, got);
    if (got == want) say(LAB_GREEN, "  certo: nenhuma soma se perdeu");
    else say(LAB_RED, "  perdidas: %ld somas", want - got);
    say(LAB_DIM, "  (%lu ms)\n", timer_ms() - t0);
    if (!lock) say(LAB_DIM, "Rode de novo: o resultado muda a cada vez. Depois compare com \"corrida trava\".\n");
}

/* ================================================================== */
/* prodcons: fila limitada, semáforos e join (caps. 27, 30 e 31)        */
/* ================================================================== */
#define PC_WORKERS 4
#define PC_BUFFER  64
#define PC_END     (-1)

typedef struct pc_run pc_run_t;
typedef struct {
    pc_run_t *run;
    task_t *task;
    int index;
    bool producer;
    uint64_t retries;
    uint64_t cpu0, runs0, yields0, blocks0;
    task_info_t final;
} pc_worker_t;

struct pc_run {
    bool blocking, abort;
    int producers, consumers, capacity, each, delay, total;
    int ring[PC_BUFFER], head, count, peak;
    int produced, consumed, duplicates, invalid;
    uint8_t *seen;
    mutex_t queue_lock;
    sem_t slots, items, start;
    pc_worker_t workers[PC_WORKERS * 2];
};

/* A saída pertence a ESTA invocação: outro Terminal pode executar um lab
 * enquanto este bloqueia. Os trabalhadores nunca desenham nem usam `out`. */
static void pc_print(lab_out_t *o, int color, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void pc_print(lab_out_t *o, int color, const char *fmt, ...) {
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    o->write(o->ctx, color, buf);
}

/* Nos dois modos o mutex protege a mesma fila. Só a maneira de esperar por
 * espaço/dados muda: consultar e ceder a CPU, ou dormir num semáforo. */
static void pc_put(pc_run_t *r, pc_worker_t *w, int item) {
    if (r->blocking) {
        sem_wait(&r->slots);
    }
    for (;;) {
        mutex_lock(&r->queue_lock);
        if (r->blocking) ASSERT(r->count < r->capacity);
        if (r->count < r->capacity) {
            r->ring[(r->head + r->count) % r->capacity] = item;
            r->count++;
            if (r->count > r->peak) r->peak = r->count;
            if (item != PC_END) r->produced++;
            mutex_unlock(&r->queue_lock);
            if (r->blocking) sem_post(&r->items);
            return;
        }
        mutex_unlock(&r->queue_lock);
        if (w) w->retries++;
        task_yield();
    }
}

static int pc_get(pc_run_t *r, pc_worker_t *w) {
    if (r->blocking) {
        sem_wait(&r->items);
    }
    for (;;) {
        mutex_lock(&r->queue_lock);
        if (r->blocking) ASSERT(r->count > 0);
        if (r->count) {
            int item = r->ring[r->head];
            r->head = (r->head + 1) % r->capacity;
            r->count--;
            if (item != PC_END) {
                r->consumed++;
                if (item < 0 || item >= r->total) r->invalid++;
                else if (r->seen[item]) r->duplicates++;
                else r->seen[item] = 1;
            }
            mutex_unlock(&r->queue_lock);
            if (r->blocking) sem_post(&r->slots);
            return item;
        }
        mutex_unlock(&r->queue_lock);
        w->retries++;
        task_yield();
    }
}

static void pc_worker(void *arg) {
    pc_worker_t *w = arg;
    pc_run_t *r = w->run;
    sem_wait(&r->start);
    if (r->abort) return;      /* criação parcial: todos saem antes de usar a fila */
    uint64_t f = irq_save();
    task_t *self = task_current();
    w->cpu0 = self->cpu_ticks;
    w->runs0 = self->runs;
    w->yields0 = self->yields;
    w->blocks0 = self->blocks;
    irq_restore(f);
    if (w->producer) {
        for (int i = 0; i < r->each; i++) {
            if (r->delay) task_sleep((uint64_t)r->delay);
            pc_put(r, w, w->index * r->each + i);
        }
    } else {
        while (pc_get(r, w) != PC_END) { }
    }
}

static void pc_usage(lab_out_t *o) {
    pc_print(o, LAB_TEXT, "uso: prodcons <polling|semaforo> [p=N] [c=N] [b=N] [n=N] [atraso=N]\n");
    pc_print(o, LAB_DIM, "  p/c: 1..4; buffer b: 1..64; n: 1..10000 itens por produtor; atraso: 0..20 ms\n");
    pc_print(o, LAB_DIM, "  padrão: p=2 c=2 b=4 n=40 atraso=1; fila protegida por mutex nos dois modos\n");
    pc_print(o, LAB_DIM, "  prodcons teste: regressões das primitivas reais de semáforo e join\n");
}

static void lab_prodcons(lab_out_t *o, int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "teste")) { lab_sync_test(o); return; }
    if (argc < 2 || (strcmp(argv[1], "polling") && strcmp(argv[1], "semaforo"))) {
        pc_usage(o);
        return;
    }
    long p = 2, c = 2, b = 4, n = 40, delay = 1;
    for (int i = 2; i < argc; i++) {
        if (!option(argv[i], "p", &p) && !option(argv[i], "c", &c) &&
            !option(argv[i], "b", &b) && !option(argv[i], "n", &n) &&
            !option(argv[i], "atraso", &delay)) {
            pc_usage(o);
            return;
        }
    }
    if (p < 1 || p > PC_WORKERS || c < 1 || c > PC_WORKERS || b < 1 || b > PC_BUFFER ||
        n < 1 || n > 10000 || delay < 0 || delay > 20) {
        pc_usage(o);
        return;
    }
    /* O dono mantém os dados vivos mesmo se pedirem para finalizar o Terminal.
     * Este mutex privado nunca é adquirido pelos trabalhadores. */
    mutex_t lifetime = {0};
    mutex_lock(&lifetime);
    pc_run_t *r = kcalloc(1, sizeof(*r));
    if (!r) { pc_print(o, LAB_RED, "prodcons: sem memória\n"); mutex_unlock(&lifetime); return; }
    r->seen = kcalloc((size_t)(p * n), 1);
    if (!r->seen) { kfree(r); pc_print(o, LAB_RED, "prodcons: sem memória\n"); mutex_unlock(&lifetime); return; }
    r->blocking = !strcmp(argv[1], "semaforo");
    r->producers = (int)p; r->consumers = (int)c; r->capacity = (int)b;
    r->each = (int)n; r->delay = (int)delay; r->total = (int)(p * n);
    sem_init(&r->slots, (uint64_t)b);
    sem_init(&r->items, 0);
    sem_init(&r->start, 0);

    pc_print(o, LAB_YELLOW, "Produtores/consumidores: %s, p=%ld c=%ld b=%ld n=%ld atraso=%ld ms\n",
             argv[1], p, c, b, n, delay);
    pc_print(o, LAB_DIM, "Esperado: %d IDs, cada um consumido uma vez; fila vazia ao terminar.\n", r->total);
    int created = 0;
    for (int i = 0; i < p + c; i++) {
        pc_worker_t *w = &r->workers[i];
        w->run = r; w->producer = i < p; w->index = i < p ? i : i - (int)p;
        char name[32];
        snprintf(name, sizeof(name), "prodcons %s %d", w->producer ? "P" : "C", w->index + 1);
        /* Até poderem executar, já estarão protegidos contra kill. Finalizar
         * um só deixaria os colegas esperando itens que nunca chegariam. */
        uint64_t f = irq_save();
        w->task = task_create_joinable(name, pc_worker, w);
        if (w->task) w->task->system = true;
        irq_restore(f);
        if (!w->task) { r->abort = true; break; }
        created++;
    }
    uint64_t start = timer_ms();
    for (int i = 0; i < created; i++) sem_post(&r->start);
    bool joined = true;
    /* Primeiro todos os produtores. Só depois vêm os sentinelas: nenhum
     * consumidor sai enquanto ainda pode chegar um item de trabalho. */
    for (int i = 0; i < created && i < p; i++)
        if (!task_join(r->workers[i].task, &r->workers[i].final)) joined = false;
    if (!r->abort)
        for (int i = 0; i < c; i++) pc_put(r, NULL, PC_END);
    for (int i = (int)p; i < created; i++)
        if (!task_join(r->workers[i].task, &r->workers[i].final)) joined = false;

    int missing = 0;
    for (int i = 0; i < r->total; i++) if (!r->seen[i]) missing++;
    bool ok = joined && !r->abort && !missing && !r->duplicates && !r->invalid && !r->count &&
              r->produced == r->total && r->consumed == r->total && r->peak <= r->capacity;
    if (r->abort) pc_print(o, LAB_RED, "Não foi possível criar todas as tarefas; experiência cancelada.\n");
    pc_print(o, LAB_TEXT, "  produzidos: %d; consumidos: %d; ausentes: %d; duplicados: %d; inválidos: %d\n",
             r->produced, r->consumed, missing, r->duplicates, r->invalid);
    pc_print(o, LAB_TEXT, "  fila final: %d; pico: %d/%d; tempo: %lu ms\n",
             r->count, r->peak, r->capacity, timer_ms() - start);
    pc_print(o, LAB_DIM, "  tarefa    CPU(ms)    entradas     yields   bloqueios   consultas vazias\n");
    uint64_t cpu = 0, runs = 0, yields = 0, blocks = 0, retries = 0;
    for (int i = 0; i < created; i++) {
        pc_worker_t *w = &r->workers[i];
        if (r->abort) continue;
        pc_print(o, LAB_TEXT, "  %s%d    %9lu %11lu %10lu %11lu %17lu\n",
                 w->producer ? "P" : "C", w->index + 1, w->final.cpu_ms - w->cpu0,
                 w->final.runs - w->runs0 + 1, w->final.yields - w->yields0,
                 w->final.blocks - w->blocks0, w->retries);
        cpu += w->final.cpu_ms - w->cpu0;
        runs += w->final.runs - w->runs0 + 1;
        yields += w->final.yields - w->yields0;
        blocks += w->final.blocks - w->blocks0;
        retries += w->retries;
    }
    pc_print(o, LAB_TEXT, "  total %9lu %11lu %10lu %11lu %17lu\n", cpu, runs, yields, blocks, retries);
    pc_print(o, LAB_DIM, "CPU: amostragem de 1 ms; 0 ms pode significar execução menor que um tick.\n");
    pc_print(o, LAB_DIM, "Métricas dos trabalhadores, sem a barreira inicial; bloqueios não contam sleep.\n");
    pc_print(o, ok ? LAB_GREEN : LAB_RED, "Verificação de integridade: %s\n", ok ? "OK" : "FALHOU");
    kprintf("prodcons: %s modo=%s p=%d c=%d b=%d n=%d atraso=%d total=%d\n",
            ok ? "OK" : "FALHOU", argv[1], r->producers, r->consumers, r->capacity, r->each, r->delay, r->total);
    kfree(r->seen);
    kfree(r);
    mutex_unlock(&lifetime);
}

/* ================================================================== */
/* stat: metadados de um arquivo (caps. 39 e 40)                       */
/* ================================================================== */
static void lab_stat(int argc, char **argv) {
    if (argc < 2) { txt("uso: stat <caminho>\n"); return; }
    vnode_t *v = vfs_lookup(out->cwd, argv[1]);
    if (!v) { say(LAB_RED, "stat: '%s' não existe\n", argv[1]); return; }
    char path[VFS_PATH_MAX];
    vfs_path(v, path, sizeof(path));
    datetime_t dt;
    time_to_datetime(v->mtime, &dt);
    say(LAB_YELLOW, "%s\n", path);
    txt("  tipo:        %s\n", v->type == VFS_DIR ? "pasta" : "arquivo");
    if (v->type == VFS_DIR) {
        txt("  itens:       %d\n", vfs_child_count(v));
    } else {
        txt("  tamanho:     %lu bytes\n", v->size);
        txt("  reservado:   %lu bytes, num bloco só do heap em %p\n", v->capacity, v->data);
    }
    txt("  modificado:  %02d/%02d/%04d %02d:%02d\n", dt.day, dt.month, dt.year, dt.hour, dt.minute);
    txt("  vnode em:    %p (pai: %s)\n", (void *)v, v->parent ? v->parent->name : "nenhum, é a raiz");
    say(LAB_DIM, "No ramfs, o vnode faz o papel do inode: metadados e um ponteiro para os dados.\n");
    say(LAB_DIM, "Num disco, o inode guardaria os números dos blocos onde os dados estão.\n");
}

/* ================================================================== */
static void lab_index(void) {
    static const char *const rows[][3] = {
        { "Virtualização da CPU", NULL, NULL },
        { "trocas", "caps. 6-7", "trocas de contexto de verdade, por tarefa" },
        { "escalonar <política> ...", "caps. 7-9", "FIFO, SJF, STCF, RR, MLFQ e loteria" },
        { "Memória", NULL, NULL },
        { "heap [blocos|teste]", "cap. 17", "espaço livre: blocos, divisão e junção" },
        { "traduzir [endereço]", "caps. 18-20", "o caminho de um endereço pelas tabelas" },
        { "paginas <política> ...", "cap. 22", "FIFO, LRU, OPT, relógio; paginas belady" },
        { "Concorrência", NULL, NULL },
        { "corrida [trava] [N]", "caps. 26-28", "condição de corrida e o mutex" },
        { "prodcons <modo> ...", "caps. 27-31", "fila limitada: polling ou semáforos, join e métricas" },
        { "Persistência", NULL, NULL },
        { "disco <política> ...", "cap. 37", "FIFO, SSTF, SCAN e C-SCAN" },
        { "stat <caminho>", "caps. 39-40", "metadados de um arquivo (o \"inode\")" },
        { "som teste", "cap. 36", "E/S por DMA: a placa lê o som da memória" },
    };
    say(LAB_YELLOW, "Laboratórios do OSTEP\n");
    say(LAB_DIM, "Livro grátis: pages.cs.wisc.edu/~remzi/OSTEP. Guia: app Estudos ou ~/Documentos/OSTEP.\n");
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        if (!rows[i][1]) { say(LAB_GREEN, "%s\n", rows[i][0]); continue; }
        /* a largura do printf conta bytes; "í" e "ç" têm dois em UTF-8, então
         * o alinhamento conta caracteres (bytes que não são 10xxxxxx) */
        int chars = 0;
        for (const char *p = rows[i][0]; *p; p++) chars += (*p & 0xC0) != 0x80;
        say(LAB_CYAN, "  %s%*s", rows[i][0], 26 - chars, "");
        say(LAB_DIM, "%-12s", rows[i][1]);
        txt("%s\n", rows[i][2]);
    }
}

bool lab_run(lab_out_t *o, int argc, char **argv) {
    if (!strcmp(argv[0], "prodcons")) { lab_prodcons(o, argc, argv); return true; }
    out = o;
    const char *c = argv[0];
    if (!strcmp(c, "ostep")) lab_index();
    else if (!strcmp(c, "escalonar")) lab_escalonar(argc, argv);
    else if (!strcmp(c, "paginas") || !strcmp(c, "páginas")) lab_paginas(argc, argv);
    else if (!strcmp(c, "disco")) lab_disco(argc, argv);
    else if (!strcmp(c, "traduzir")) lab_traduzir(argc, argv);
    else if (!strcmp(c, "heap")) lab_heap(argc, argv);
    else if (!strcmp(c, "trocas")) lab_trocas();
    else if (!strcmp(c, "corrida")) lab_corrida(argc, argv);
    else if (!strcmp(c, "stat")) lab_stat(argc, argv);
    else return false;
    return true;
}
