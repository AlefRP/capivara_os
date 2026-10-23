/* Gerenciador de Tarefas: lista de tarefas e gráficos de desempenho. */
#include "apps/apps.h"
#include "gui/ui.h"
#include "dev/keyboard.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "sched/task.h"
#include "arch/x86_64/pit.h"
#include "lib/string.h"
#include "lib/printf.h"

#define TAB_H 44
#define HEAD_H 30
#define ROW_H 28
#define HISTORY 60

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

typedef struct {
    window_t *win;
    task_info_t tasks[64];
    int ntasks;
    int tab, sel_id, hover_row, hover;
    ui_list_t list;
    uint8_t cpu_hist[HISTORY], mem_hist[HISTORY];
    int nhist;
    bool quit;
} tm_t;

static void sample(tm_t *t) {
    t->ntasks = task_snapshot(t->tasks, 64);
    int idle = 0;
    for (int i = 0; i < t->ntasks; i++) if (t->tasks[i].id == 0) idle = (int)t->tasks[i].cpu_percent;
    int cpu = imax(0, imin(100, 100 - idle));
    uint64_t total = pmm_total_bytes();
    int mem = (int)((total - pmm_free_bytes()) * 100 / total);
    if (t->nhist == HISTORY) {
        memmove(t->cpu_hist, t->cpu_hist + 1, HISTORY - 1);
        memmove(t->mem_hist, t->mem_hist + 1, HISTORY - 1);
        t->nhist--;
    }
    t->cpu_hist[t->nhist] = (uint8_t)cpu;
    t->mem_hist[t->nhist++] = (uint8_t)mem;
    t->list.count = t->ntasks;
}

static const char *state_name(task_state_t s) {
    switch (s) {
    case TASK_READY: return "Pronta";
    case TASK_RUNNING: return "Em execução";
    case TASK_SLEEPING: return "Dormindo";
    case TASK_BLOCKED: return "Aguardando";
    default: return "Encerrada";
    }
}

static rect_t list_rect(tm_t *t) {
    canvas_t *c = win_canvas(t->win);
    return R(0, TAB_H + HEAD_H, c->w, c->h - TAB_H - HEAD_H - 56);
}
static rect_t end_button(tm_t *t) {
    canvas_t *c = win_canvas(t->win);
    return R(c->w - 16 - 150, c->h - 44, 150, 32);
}
static rect_t tab_rect(int i) { return R(12 + i * 130, 6, 124, 34); }

static task_info_t *selected(tm_t *t) {
    for (int i = 0; i < t->ntasks; i++) if (t->tasks[i].id == t->sel_id) return &t->tasks[i];
    return NULL;
}

static void graph(canvas_t *c, rect_t r, uint8_t *hist, int n, uint32_t color, const char *title, const char *value) {
    gfx_round_fill(c, r.x, r.y, r.w, r.h, 6, theme.surface);
    gfx_round_rect(c, r.x, r.y, r.w, r.h, 6, theme.border);
    font_draw(c, r.x + 16, r.y + 12, title, theme.text, FONT_BOLD);
    font_draw(c, r.x + r.w - 16 - font_width(value, 0), r.y + 12, value, theme.text_dim, 0);
    rect_t g = R(r.x + 16, r.y + 40, r.w - 32, r.h - 56);
    gfx_rect(c, g.x, g.y, g.w, g.h, theme.border);
    for (int i = 1; i < 4; i++) gfx_hline(c, g.x, g.y + g.h * i / 4, g.w, color_with_alpha(theme.border, 0x80));
    float step = (float)g.w / (HISTORY - 1);
    int off = HISTORY - n;
    for (int i = 0; i < n; i++) {
        float x = g.x + (off + i) * step;
        float y = g.y + g.h - 1 - hist[i] * (g.h - 2) / 100.0f;
        gfx_line(c, x, y, x, g.y + g.h - 1, step + 1, color_with_alpha(color, 0x30));
        if (i) {
            float px = g.x + (off + i - 1) * step;
            float py = g.y + g.h - 1 - hist[i - 1] * (g.h - 2) / 100.0f;
            gfx_line(c, px, py, x, y, 1.6f, color);
        }
    }
}

static void draw(tm_t *t) {
    canvas_t *c = win_canvas(t->win);
    gfx_clear(c, theme.window_bg);
    static const char *tabs[] = { "Processos", "Desempenho" };
    for (int i = 0; i < 2; i++) {
        rect_t r = tab_rect(i);
        if (i == t->tab || t->hover == 100 + i) gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, i == t->tab ? theme.pressed : theme.hover);
        ui_text_center(c, r, tabs[i], theme.text, i == t->tab ? FONT_BOLD : 0);
        if (i == t->tab) gfx_round_fill(c, r.x + r.w / 2 - 10, r.y + r.h - 3, 20, 3, 1.5f, theme.accent);
    }
    gfx_hline(c, 0, TAB_H, c->w, theme.border);

    if (t->tab == 0) {
        rect_t lr = list_rect(t);
        int colx[5] = { 16, c->w * 40 / 100, c->w * 50 / 100, c->w * 68 / 100, c->w * 81 / 100 };
        static const char *heads[] = { "Nome", "PID", "Status", "CPU", "Memória" };
        gfx_fill(c, 0, TAB_H + 1, c->w, HEAD_H - 1, theme.window_bg);
        for (int i = 0; i < 5; i++) font_draw(c, colx[i], TAB_H + 7, heads[i], theme.text_dim, 0);
        gfx_fill(c, lr.x, lr.y, lr.w, lr.h, theme.surface);
        rect_t saved = c->clip;
        canvas_clip(c, lr);
        for (int i = 0; i < t->ntasks; i++) {
            int y = lr.y + i * ROW_H - t->list.scroll;
            if (y + ROW_H < lr.y || y > lr.y + lr.h) continue;
            task_info_t *ti = &t->tasks[i];
            if (ti->id == t->sel_id) gfx_fill(c, 0, y, c->w, ROW_H, theme.selection);
            else if (i == t->hover_row) gfx_fill(c, 0, y, c->w, ROW_H, theme.hover);
            /* intensidade de CPU, como um "mapa de calor" */
            if (ti->cpu_percent) gfx_fill(c, colx[3] - 8, y, colx[4] - colx[3], ROW_H,
                                          ARGB(imin(0xA0, 0x20 + ti->cpu_percent * 2), 0xF5B400));
            if (ti->mem_bytes > (4u << 20)) gfx_fill(c, colx[4] - 8, y, c->w - colx[4] + 8, ROW_H,
                                                     ARGB(imin(0x90, 0x18 + (int)(ti->mem_bytes >> 20) * 3), 0x4FA3E0));
            int icon = ti->system ? ICON_SETTINGS : ICON_FILE;
            for (int k = 0; k < app_count; k++)
                if (!strcmp(app_list[k].name, ti->name)) icon = app_list[k].icon;
            icon_draw(c, icon, colx[0], y + 6, 16);
            char buf[32];
            font_draw_ellipsis(c, colx[0] + 26, y + 6, colx[1] - colx[0] - 36, ti->name, theme.text, 0);
            snprintf(buf, sizeof(buf), "%d", ti->id);
            font_draw(c, colx[1], y + 6, buf, theme.text_dim, 0);
            font_draw(c, colx[2], y + 6, state_name(ti->state), theme.text_dim, 0);
            snprintf(buf, sizeof(buf), "%u%%", ti->cpu_percent);
            font_draw(c, colx[3], y + 6, buf, theme.text, 0);
            if (ti->mem_bytes >= (1u << 20)) snprintf(buf, sizeof(buf), "%lu,%lu MB", ti->mem_bytes >> 20, ((ti->mem_bytes >> 10) % 1024) * 10 / 1024);
            else snprintf(buf, sizeof(buf), "%lu KB", ti->mem_bytes >> 10);
            font_draw(c, colx[4], y + 6, buf, theme.text, 0);
        }
        c->clip = saved;
        ui_scrollbar(c, lr, t->ntasks * ROW_H, lr.h, t->list.scroll);
        gfx_hline(c, 0, lr.y + lr.h, c->w, theme.border);
        task_info_t *s = selected(t);
        bool can = s && !s->system;
        ui_button(c, end_button(t), "Finalizar tarefa", BTN_ACCENT, !can ? UI_DISABLED : t->hover == 200 ? UI_HOVER : UI_NORMAL);
        char info[96];
        snprintf(info, sizeof(info), "%d tarefas", t->ntasks);
        font_draw(c, 16, c->h - 36, info, theme.text_dim, 0);
        if (s && s->system) font_draw(c, 120, c->h - 36, "(tarefa do sistema: não pode ser finalizada)", theme.text_dim, 0);
    } else {
        int w = c->w - 32, h = (c->h - TAB_H - 48) / 2;
        char v[64];
        snprintf(v, sizeof(v), "%d%%", t->nhist ? t->cpu_hist[t->nhist - 1] : 0);
        graph(c, R(16, TAB_H + 16, w, h - 8), t->cpu_hist, t->nhist, theme.accent, "CPU", v);
        uint64_t total = pmm_total_bytes(), used = total - pmm_free_bytes();
        snprintf(v, sizeof(v), "%lu / %lu MiB (%d%%)", used >> 20, total >> 20, t->nhist ? t->mem_hist[t->nhist - 1] : 0);
        graph(c, R(16, TAB_H + 16 + h, w, h - 8), t->mem_hist, t->nhist, HEX(0x8E44AD), "Memória", v);
    }
    win_invalidate(t->win);
}

void taskmgr_main(void *arg) {
    if (arg) kfree(arg);
    tm_t *t = kcalloc(1, sizeof(tm_t));
    t->sel_id = -1;
    t->hover_row = t->hover = -1;
    t->list.row_h = ROW_H;
    t->win = win_create("Gerenciador de Tarefas", ICON_TASKMGR, WIN_AUTO, WIN_AUTO, 660, 500, WF_APP);
    win_set_min_size(t->win, 480, 360);
    sample(t);
    ev_set_timer(1000);
    draw(t);
    while (!t->quit) {
        event_t e;
        gui_wait(&e);
        rect_t lr = list_rect(t);
        switch (e.type) {
        case EV_TIMER: sample(t); break;
        case EV_MOUSE_MOVE: {
            int h = -1, row = -1;
            for (int i = 0; i < 2; i++) if (ui_hit(tab_rect(i), &e)) h = 100 + i;
            if (t->tab == 0 && ui_hit(end_button(t), &e)) h = 200;
            if (t->tab == 0) row = ui_list_hit(&t->list, lr, e.y);
            if (h == t->hover && row == t->hover_row) continue;
            t->hover = h;
            t->hover_row = row;
            break;
        }
        case EV_MOUSE_LEAVE: t->hover = t->hover_row = -1; break;
        case EV_MOUSE_DOWN:
            for (int i = 0; i < 2; i++) if (ui_hit(tab_rect(i), &e)) t->tab = i;
            if (t->tab == 0 && ui_hit(lr, &e)) {
                int i = ui_list_hit(&t->list, lr, e.y);
                t->sel_id = i >= 0 ? t->tasks[i].id : -1;
            }
            if (t->tab == 0 && ui_hit(end_button(t), &e)) {
                task_info_t *s = selected(t);
                if (s && !s->system) {
                    task_t *victim = task_find(s->id);
                    if (victim) task_kill(victim);
                    t->sel_id = -1;
                    task_sleep(50);
                    sample(t);
                }
            }
            break;
        case EV_MOUSE_WHEEL:
            ui_list_wheel(&t->list, lr, e.wheel);
            break;
        case EV_KEY_DOWN:
            if (e.key == KEY_DELETE && selected(t) && !selected(t)->system) {
                task_t *victim = task_find(t->sel_id);
                if (victim) task_kill(victim);
            } else if (e.key == KEY_TAB) t->tab = !t->tab;
            break;
        case EV_RESIZE: case EV_THEME: break;
        case EV_CLOSE: t->quit = true; continue;
        default: continue;
        }
        draw(t);
    }
    win_destroy(t->win);
    kfree(t);
}
