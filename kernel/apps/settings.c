/* Configurações: personalização, informações do sistema, data e hora, sobre. */
#include "apps/apps.h"
#include "version.h"
#include "gui/ui.h"
#include "dev/rtc.h"
#include "dev/keyboard.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "sched/task.h"
#include "arch/x86_64/pit.h"
#include "boot/bootinfo.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "apps/session.h"
#include "gui/wallpaper.h"
#include "apps/notify.h"

#define NAV_W 240

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

enum { P_PERSONAL, P_ACCOUNTS, P_SYSTEM, P_TIME, P_ABOUT, P_COUNT };
static const struct { const char *name; int icon; } pages[P_COUNT] = {
    { "Personalização", ICON_PAINT }, { "Contas", ICON_USER }, { "Sistema", ICON_COMPUTER },
    { "Data e hora", ICON_CLOCK }, { "Sobre", ICON_INFO },
};

typedef struct {
    window_t *win;
    int page, hover, press;
    rect_t hot[32];
    int hot_id[32];
    int nhot;
    bool quit;
} st_t;

static void hot(st_t *s, rect_t r, int id) {
    if (s->nhot < 32) { s->hot[s->nhot] = r; s->hot_id[s->nhot++] = id; }
}

static void card(canvas_t *c, int x, int y, int w, int h) {
    gfx_round_fill(c, x, y, w, h, 6, theme.surface);
    gfx_round_rect(c, x, y, w, h, 6, theme.border);
}

/* miniaturas simplificadas dos papéis de parede */
static void wallpaper_thumb(canvas_t *c, int id, int x, int y, int w, int h) {
    rect_t saved = c->clip;
    canvas_clip(c, rect_intersect(saved, R(x, y, w, h)));
    switch (id) {
    case 0: {
        /* a cena em pixel art é desenhada de verdade (e guardada para reuso) */
        static canvas_t *cache[2];          /* prévia grande e miniatura da lista */
        static int next_slot;
        canvas_t *thumb = NULL;
        for (int k = 0; k < 2; k++)
            if (cache[k] && cache[k]->w == w && cache[k]->h == h) thumb = cache[k];
        if (!thumb) {
            canvas_destroy(cache[next_slot]);
            thumb = cache[next_slot] = canvas_create(w, h);
            next_slot ^= 1;
            if (thumb) wallpaper_capivara(thumb, false);
        }
        if (thumb) gfx_blit(c, x, y, thumb, 0, 0, w, h);
        break;
    }
    case 1:
        gfx_vgradient(c, x, y, w, h, HEX(0x0E2A6B), HEX(0x3B1F6E));
        for (int k = 0; k < 4; k++) {
            static const uint32_t cols[4] = { 0x2D7FF0, 0x7B5CF0, 0xD05AC8, 0xF7A26B };
            for (int i = 0; i < w; i++) {
                int top = y + (int)(h * (0.38f + 0.13f * k) + h * 0.06f * gfx_sin(i * 6.0f / w + k * 1.7f));
                gfx_fill(c, x + i, top, 1, y + h - top, ARGB(0x90, cols[k]));
            }
        }
        break;
    case 2:
        gfx_vgradient(c, x, y, w, h, HEX(0x0B2E59), HEX(0x177E89));
        gfx_circle(c, x + w * 0.78f, y + h * 0.3f, h * 0.55f, ARGB(0x70, 0xF7C948));
        gfx_circle(c, x + w * 0.2f, y + h * 0.9f, h * 0.5f, ARGB(0x60, 0xE86A92));
        break;
    case 3:
        gfx_vgradient(c, x, y, w, h, HEX(0x050B18), HEX(0x0E2A3B));
        for (int i = 0; i < w; i += 3)
            gfx_line(c, x + i, y + h * 0.38f + 6 * gfx_sin(i / 14.0f), x + i, y + h * 0.38f + 6 * gfx_sin(i / 14.0f) + 18,
                     2, ARGB(0x70, 0x5CE6B0));
        break;
    case 4:
        gfx_vgradient(c, x, y, w, h * 6 / 10, HEX(0x2E1A47), HEX(0xE0607E));
        gfx_vgradient(c, x, y + h * 6 / 10, w, h - h * 6 / 10, HEX(0xF79A5B), HEX(0xFFD39B));
        gfx_circle(c, x + w * 0.62f, y + h * 0.6f, h * 0.18f, HEX(0xFFF1C1));
        gfx_fill(c, x, y + h * 3 / 4, w, h / 4, HEX(0x2A1633));
        break;
    default:
        gfx_vgradient(c, x, y, w, h, color_mix(theme.accent, HEX(0), 60), color_mix(theme.accent, HEX(0), 150));
        break;
    }
    c->clip = saved;
}

static void draw_personal(st_t *s, canvas_t *c, int x0, int y) {
    int w = c->w - x0 - 32;
    /* prévia */
    wallpaper_thumb(c, theme.wallpaper, x0, y, 280, 170);
    gfx_fill(c, x0, y, 280, 8, theme.taskbar_bg);
    gfx_round_fill(c, x0 + 90, y + 152, 100, 14, 4, theme.dock_bg);
    gfx_round_fill(c, x0 + 70, y + 30, 140, 100, 6, theme.window_bg);
    gfx_circle(c, x0 + 78, y + 37, 2.5f, HEX(0xFF5F57));
    gfx_circle(c, x0 + 86, y + 37, 2.5f, HEX(0xFEBC2E));
    gfx_circle(c, x0 + 94, y + 37, 2.5f, HEX(0x28C840));
    gfx_fill(c, x0 + 80, y + 60, 60, 8, theme.accent);
    gfx_round_rect(c, x0, y, 280, 170, 6, theme.border);

    int tx = x0 + 304;
    font_draw(c, tx, y, "Modo de cor", theme.text, FONT_BOLD);
    for (int i = 0; i < 2; i++) {
        rect_t r = R(tx + i * 150, y + 28, 140, 96);
        bool sel = theme.dark == (i == 1);
        gfx_round_fill(c, r.x, r.y, r.w, 70, 6, i ? HEX(0x202020) : HEX(0xF3F3F3));
        gfx_round_fill(c, r.x + 16, r.y + 14, r.w - 32, 42, 4, i ? HEX(0x2D2D2D) : HEX(0xFFFFFF));
        gfx_fill(c, r.x + 24, r.y + 24, 40, 6, theme.accent);
        font_draw(c, r.x + 8, r.y + 76, i ? "Escuro" : "Claro", theme.text, 0);
        gfx_round_rect(c, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 7, sel ? theme.accent : (s->hover == 10 + i ? theme.border_strong : 0));
        if (sel) gfx_round_rect(c, r.x - 1, r.y - 1, r.w + 2, r.h + 2, 7, theme.accent);
        hot(s, r, 10 + i);
    }

    y += 200;
    font_draw(c, x0, y, "Cor de destaque", theme.text, FONT_BOLD);
    for (int i = 0; i < ACCENT_COUNT; i++) {
        int cx = x0 + 20 + i * 56, cy = y + 46;
        if (i == theme.accent_index) gfx_ring(c, cx, cy, 20, 2.5f, theme.text);
        else if (s->hover == 20 + i) gfx_ring(c, cx, cy, 20, 1.5f, theme.border_strong);
        gfx_circle(c, cx, cy, 15, accent_colors[i]);
        hot(s, R(cx - 22, cy - 22, 44, 44), 20 + i);
    }
    font_draw(c, x0 + ACCENT_COUNT * 56 + 12, y + 38, accent_names[theme.accent_index], theme.text_dim, 0);

    y += 96;
    font_draw(c, x0, y, "Papel de parede", theme.text, FONT_BOLD);
    int tw = imin(108, (w - 5 * 10) / 6);
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        rect_t r = R(x0 + i * (tw + 10), y + 28, tw, tw * 3 / 4);
        wallpaper_thumb(c, i, r.x, r.y, r.w, r.h);
        if (i == theme.wallpaper) {
            gfx_round_rect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 7, theme.accent);
            gfx_round_rect(c, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 7, theme.accent);
        } else if (s->hover == 30 + i) {
            gfx_round_rect(c, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 7, theme.border_strong);
        }
        font_draw(c, r.x, r.y + r.h + 8, wallpaper_names[i], theme.text_dim, 0);
        hot(s, r, 30 + i);
    }
}

static void row(canvas_t *c, int x, int *y, int w, const char *label, const char *value) {
    card(c, x, *y, w, 52);
    font_draw(c, x + 20, *y + 18, label, theme.text, 0);
    font_draw_ellipsis(c, x + 230, *y + 18, w - 250, value, theme.text_dim, 0);
    *y += 58;
}

static void draw_system(st_t *s, canvas_t *c, int x0, int y) {
    int w = c->w - x0 - 32;
    char buf[160];
    uint64_t total = pmm_total_bytes(), used = total - pmm_free_bytes();
    card(c, x0, y, w, 96);
    font_draw(c, x0 + 20, y + 16, "Memória (RAM)", theme.text, FONT_BOLD);
    snprintf(buf, sizeof(buf), "%lu MiB em uso de %lu MiB", used >> 20, total >> 20);
    font_draw(c, x0 + 20, y + 40, buf, theme.text_dim, 0);
    int bw = w - 40;
    gfx_round_fill(c, x0 + 20, y + 68, bw, 8, 4, theme.border);
    gfx_round_fill(c, x0 + 20, y + 68, imax(8, (int)(bw * used / total)), 8, 4, theme.accent);
    y += 108;
    heap_stats_t hs;
    heap_stats(&hs);
    row(c, x0, &y, w, "Processador", cpu_brand());
    snprintf(buf, sizeof(buf), "%u x %u, 32 bits por pixel", bootinfo.fb_width, bootinfo.fb_height);
    row(c, x0, &y, w, "Tela", buf);
    snprintf(buf, sizeof(buf), "%lu KiB em uso (%lu blocos)", hs.used >> 10, hs.blocks);
    row(c, x0, &y, w, "Heap do kernel", buf);
    task_info_t tl[64];
    uint64_t up = timer_ms() / 1000;
    snprintf(buf, sizeof(buf), "%d tarefas, ligado há %luh %02lum %02lus", task_snapshot(tl, 64), up / 3600, (up / 60) % 60, up % 60);
    row(c, x0, &y, w, "Execução", buf);
    row(c, x0, &y, w, "Carregador de boot", bootinfo.bootloader);
}

static void draw_time(st_t *s, canvas_t *c, int x0, int y) {
    int w = c->w - x0 - 32;
    datetime_t dt;
    time_to_datetime(time_now(), &dt);
    char buf[96];
    card(c, x0, y, w, 120);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
    font_draw(c, x0 + 24, y + 24, buf, theme.text, FONT_2X | FONT_BOLD);
    snprintf(buf, sizeof(buf), "%s, %d de %s de %d", weekday_name(dt.weekday), dt.day, month_name(dt.month), dt.year);
    font_draw(c, x0 + 24, y + 72, buf, theme.text_dim, 0);
    rect_t b = R(x0 + w - 200, y + 44, 176, 34);
    ui_button(c, b, "Ajustar data e hora", BTN_ACCENT, s->hover == 40 ? UI_HOVER : UI_NORMAL);
    hot(s, b, 40);
    y += 136;
    row(c, x0, &y, w, "Fuso horário", "Hora local (o relógio do hardware guarda a hora local)");
    row(c, x0, &y, w, "Formato", "dd/mm/aaaa, 24 horas");
    bool us = keyboard_layout() == KB_LAYOUT_US;
    row(c, x0, &y, w, "Layout do teclado", us ? "Inglês (EUA)" : "Português (Brasil ABNT2), acentos por tecla morta");
    rect_t kb = R(x0 + w - 180, y - 58 + 10, 164, 32);
    ui_button(c, kb, us ? "Usar ABNT2" : "Usar EUA", BTN_STANDARD, s->hover == 41 ? UI_HOVER : UI_NORMAL);
    hot(s, kb, 41);
}

static void draw_accounts(st_t *s, canvas_t *c, int x0, int y) {
    int w = c->w - x0 - 32;
    card(c, x0, y, w, 110);
    icon_draw(c, ICON_USER, x0 + 20, y + 15, 80);
    font_draw(c, x0 + 120, y + 30, session_user(), theme.text, FONT_2X | FONT_BOLD);
    font_draw(c, x0 + 120, y + 70, "Conta local · administrador", theme.text_dim, 0);
    y += 126;
    row(c, x0, &y, w, "Nome de exibição", session_user());
    rect_t b1 = R(x0 + w - 140, y - 58 + 10, 124, 32);
    ui_button(c, b1, "Alterar", BTN_STANDARD, s->hover == 50 ? UI_HOVER : UI_NORMAL);
    hot(s, b1, 50);
    row(c, x0, &y, w, "Senha", session_has_password() ? "Definida" : "Nenhuma (entra só clicando em Entrar)");
    rect_t b2 = R(x0 + w - 140, y - 58 + 10, 124, 32);
    ui_button(c, b2, session_has_password() ? "Alterar" : "Definir", BTN_ACCENT, s->hover == 51 ? UI_HOVER : UI_NORMAL);
    hot(s, b2, 51);
    if (session_has_password()) {
        rect_t b3 = R(b2.x - 132, b2.y, 124, 32);
        ui_button(c, b3, "Remover", BTN_STANDARD, s->hover == 52 ? UI_HOVER : UI_NORMAL);
        hot(s, b3, 52);
    }
    row(c, x0, &y, w, "Tela de bloqueio", "Super+L bloqueia o computador");
    rect_t b4 = R(x0 + w - 140, y - 58 + 10, 124, 32);
    ui_button(c, b4, "Bloquear agora", BTN_STANDARD, s->hover == 53 ? UI_HOVER : UI_NORMAL);
    hot(s, b4, 53);
    ui_wrap(c, R(x0, y + 8, w, 60),
            "A senha é guardada como hash com \"sal\" (nunca em texto puro). Como os arquivos ficam na "
            "memória, ela vale até o computador ser desligado.", theme.text_dim, 0, true);
}

static void account_action(st_t *s, int id) {
    char a[64] = "", b[64] = "";
    if (id == 50) {
        strlcpy(a, session_user(), sizeof(a));
        if (ui_input(s->win, "Nome de exibição", "Como você quer ser chamado?", a, sizeof(a)) && a[0]) session_set_user(a);
    } else if (id == 51) {
        if (session_has_password()) {
            if (!ui_input_ex(s->win, "Alterar senha", "Senha atual:", a, sizeof(a), true)) return;
            if (!session_check_password(a)) {
                ui_message(s->win, "Contas", "Senha atual incorreta.", "OK", NULL, NULL);
                return;
            }
            a[0] = 0;
        }
        if (!ui_input_ex(s->win, "Definir senha", "Nova senha:", a, sizeof(a), true) || !a[0]) return;
        if (!ui_input_ex(s->win, "Definir senha", "Digite a senha de novo:", b, sizeof(b), true)) return;
        if (strcmp(a, b)) {
            ui_message(s->win, "Contas", "As senhas não conferem.", "OK", NULL, NULL);
            return;
        }
        session_set_password(a);
        notify("Configurações", "Senha definida", "Ela será pedida na tela de bloqueio (Super+L).", ICON_USER, NULL);
    } else if (id == 52) {
        if (!ui_input_ex(s->win, "Remover senha", "Senha atual:", a, sizeof(a), true)) return;
        if (!session_check_password(a)) {
            ui_message(s->win, "Contas", "Senha incorreta.", "OK", NULL, NULL);
            return;
        }
        session_set_password("");
    } else if (id == 53) {
        wm_shell_command(SHELL_LOCK);
    }
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
}

static void draw_about(st_t *s, canvas_t *c, int x0, int y) {
    int w = c->w - x0 - 32;
    card(c, x0, y, w, 120);
    icon_draw(c, ICON_LOGO, x0 + 20, y + 20, 80);
    font_draw(c, x0 + 120, y + 26, "Capivara OS", theme.text, FONT_2X | FONT_BOLD);
    font_draw(c, x0 + 120, y + 66, "Versão " CAPIVARA_VERSION " · fase 1 (kernel + interface gráfica)", theme.text_dim, 0);
    font_draw(c, x0 + 120, y + 86, "x86_64 · kernel monolítico próprio", theme.text_dim, 0);
    y += 136;
    card(c, x0, y, w, 290);
    ui_wrap(c, R(x0 + 20, y + 16, w - 40, 260),
            "Sistema operacional educacional escrito do zero. O kernel, os drivers, a interface gráfica e os "
            "aplicativos são código próprio.\n\n"
            "Ferramentas GNU: GCC 14 e binutils (compilador cruzado x86_64-elf), GNU Make, GNU GRUB 2 (boot "
            "Multiboot2) e a fonte GNU Unifont.\n\n"
            "Processo clean room: os conceitos do Unix/Linux (tarefas, escalonador, sistema de arquivos virtual) "
            "e as convenções visuais conhecidas de outros sistemas foram descritos em especificações próprias "
            "(pasta docs/) e implementados sem copiar código de terceiros.",
            theme.text, 0, true);
}

static void draw(st_t *s) {
    canvas_t *c = win_canvas(s->win);
    s->nhot = 0;
    gfx_clear(c, theme.window_bg);

    /* navegação lateral */
    icon_draw(c, ICON_USER, 20, 20, 48);
    font_draw(c, 80, 24, "Usuário", theme.text, FONT_BOLD);
    font_draw(c, 80, 44, "Conta local", theme.text_dim, 0);
    for (int i = 0; i < P_COUNT; i++) {
        rect_t r = R(12, 92 + i * 42, NAV_W - 24, 38);
        if (i == s->page || s->hover == i) gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, i == s->page ? theme.pressed : theme.hover);
        if (i == s->page) gfx_round_fill(c, r.x, r.y + 10, 3, r.h - 20, 1.5f, theme.accent);
        icon_draw(c, pages[i].icon, r.x + 14, r.y + 11, 16);
        font_draw(c, r.x + 42, r.y + 11, pages[i].name, theme.text, 0);
        hot(s, r, i);
    }

    int x0 = NAV_W + 16;
    font_draw(c, x0, 24, pages[s->page].name, theme.text, FONT_2X | FONT_BOLD);
    switch (s->page) {
    case P_PERSONAL: draw_personal(s, c, x0, 84); break;
    case P_ACCOUNTS: draw_accounts(s, c, x0, 84); break;
    case P_SYSTEM: draw_system(s, c, x0, 84); break;
    case P_TIME: draw_time(s, c, x0, 84); break;
    case P_ABOUT: draw_about(s, c, x0, 84); break;
    }
    win_invalidate(s->win);
}

static void adjust_time(st_t *s) {
    datetime_t dt;
    time_to_datetime(time_now(), &dt);
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", dt.day, dt.month, dt.year, dt.hour, dt.minute);
    if (!ui_input(s->win, "Ajustar data e hora", "Data e hora (dd/mm/aaaa hh:mm):", buf, sizeof(buf))) return;
    const char *p = buf;
    datetime_t n = { 0 };
    n.day = (int)strtol(p, &p, 10); if (*p) p++;
    n.month = (int)strtol(p, &p, 10); if (*p) p++;
    n.year = (int)strtol(p, &p, 10);
    n.hour = (int)strtol(p, &p, 10); if (*p) p++;
    n.minute = (int)strtol(p, &p, 10);
    if (n.day < 1 || n.day > 31 || n.month < 1 || n.month > 12 || n.year < 1970 || n.hour > 23 || n.minute > 59) {
        ui_message(s->win, "Data e hora", "Formato inválido. Use dd/mm/aaaa hh:mm.", "OK", NULL, NULL);
        return;
    }
    time_set(&n);
}

static void click(st_t *s, int id) {
    if (id < P_COUNT) s->page = id;
    else if (id == 10 || id == 11) wm_apply_theme(id == 11, theme.accent_index, theme.wallpaper);
    else if (id >= 20 && id < 20 + ACCENT_COUNT) wm_apply_theme(theme.dark, id - 20, theme.wallpaper);
    else if (id >= 30 && id < 30 + WALLPAPER_COUNT) wm_apply_theme(theme.dark, theme.accent_index, id - 30);
    else if (id >= 50 && id <= 53) account_action(s, id);
    else if (id == 40) adjust_time(s);
    else if (id == 41) keyboard_set_layout(keyboard_layout() == KB_LAYOUT_US ? KB_LAYOUT_ABNT2 : KB_LAYOUT_US);
}

void settings_main(void *arg) {
    st_t *s = kcalloc(1, sizeof(st_t));
    s->hover = s->press = -1;
    if (arg) {
        if (!strcmp(arg, "sobre")) s->page = P_ABOUT;
        else if (!strcmp(arg, "sistema")) s->page = P_SYSTEM;
        else if (!strcmp(arg, "contas")) s->page = P_ACCOUNTS;
        kfree(arg);
    }
    s->win = win_create("Configurações", ICON_SETTINGS, WIN_AUTO, WIN_AUTO, 900, 580, WF_APP);
    win_set_min_size(s->win, 760, 500);
    ev_set_timer(1000);
    draw(s);
    while (!s->quit) {
        event_t e;
        gui_wait(&e);
        int at = -1;
        for (int i = 0; i < s->nhot; i++) if (ui_hit(s->hot[i], &e)) at = s->hot_id[i];
        switch (e.type) {
        case EV_MOUSE_MOVE: if (at == s->hover) continue; s->hover = at; break;
        case EV_MOUSE_LEAVE: s->hover = -1; break;
        case EV_MOUSE_DOWN: s->press = at; continue;
        case EV_MOUSE_UP: if (at >= 0 && at == s->press) click(s, at); s->press = -1; break;
        case EV_KEY_DOWN:
            if (e.key == KEY_UP && s->page > 0) s->page--;
            else if (e.key == KEY_DOWN && s->page < P_COUNT - 1) s->page++;
            else continue;
            break;
        case EV_TIMER: if (s->page != P_SYSTEM && s->page != P_TIME) continue; break;
        case EV_RESIZE: case EV_THEME: break;
        case EV_CLOSE: s->quit = true; continue;
        default: continue;
        }
        draw(s);
    }
    win_destroy(s->win);
    kfree(s);
}
