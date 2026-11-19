/*
 * ui.c — реализация абстрактного UI: кнопки, окно, прогрессбар.
 *
 * Вся графика — 9-срезами из единого изображения скина, координаты
 * элементов читаются из config/windows.cfg через readconfig.
 * Отрисовка надписей — через шрифт (font.h), графика и мышь — через
 * платформенный уровень (portable_posix.h). Хардкод координат
 * элементов отсутствует.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portable_posix.h"
#include "readconfig.h"
#include "font.h"
#include "ui.h"

/* ------------------------------------------------------------ кнопки */

ui_button ui_buttons[UI_BUTTONS_MAX];
int       ui_buttons_count = 0;

int ui_button_add(int x, int y, int w, int h,
                  const char *caption_1251, ui_callback on_press)
{
    if (ui_buttons_count >= UI_BUTTONS_MAX)
        return -1;
    ui_button *b = &ui_buttons[ui_buttons_count];
    memset(b, 0, sizeof(*b));
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    if (caption_1251) {
        strncpy(b->caption, caption_1251, UI_BUTTON_CAPTION_MAX - 1);
        b->caption[UI_BUTTON_CAPTION_MAX - 1] = '\0';
    }
    b->on_press = on_press;
    b->enabled = 1;
    return ui_buttons_count++;
}

/* ------------------------------------------------------------ скин */

typedef struct {
    int x, y, w, h;
} ui_rect;

/* индексы элементов 9-срезов в массиве */
enum { P_TL, P_TR, P_BL, P_BR, P_TOP, P_BOTTOM, P_LEFT, P_RIGHT,
       P_CENTER, P_COUNT };

static plat_image *g_skin = NULL;
static ui_rect g_button[P_COUNT];        /* обычная кнопка */
static ui_rect g_button_pressed[P_COUNT]; /* нажатая кнопка */
static ui_rect g_win[P_COUNT];           /* типовое окно */
static ui_rect g_win_bg;                 /* паттерн фона окна */
static ui_rect g_hud[P_COUNT];           /* окно HUD (киберпанк) */
static ui_rect g_hud_bg;                 /* паттерн фона окна HUD */
static ui_rect g_hud_btn[P_COUNT];       /* кнопка HUD */
static ui_rect g_hud_btn_pressed[P_COUNT]; /* нажатая кнопка HUD */
static ui_rect g_pb_start, g_pb_end, g_pb_fill, g_pb_track;

static int parse_rect(const char *s, ui_rect *r)
{
    int v[4];
    const char *p = s;
    for (int i = 0; i < 4; i++) {
        char *end = NULL;
        long val = strtol(p, &end, 10);
        if (end == p)
            return -1;
        v[i] = (int)val;
        p = end;
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
    }
    r->x = v[0];
    r->y = v[1];
    r->w = v[2];
    r->h = v[3];
    return 0;
}

static int load_rect(const config *cfg, const char *path,
                     const char *section, const char *key, ui_rect *r)
{
    const char *val = config_get(cfg, section, key, NULL);
    if (!val || parse_rect(val, r) != 0) {
        fprintf(stderr, "ui_init: '%s': нет параметра '%s' в секции {%s}\n",
                path, key, section);
        return -1;
    }
    return 0;
}

static int load_pieces(const config *cfg, const char *path,
                       const char *section, ui_rect *pieces)
{
    static const char *names[P_COUNT] = {
        "corner_tl", "corner_tr", "corner_bl", "corner_br",
        "edge_top", "edge_bottom", "edge_left", "edge_right", "center"
    };
    for (int i = 0; i < P_COUNT; i++)
        if (load_rect(cfg, path, section, names[i], &pieces[i]) != 0)
            return -1;
    return 0;
}

/* Есть ли секция со срезами в конфиге (ключ corner_tl как маркер):
 * HUD-секции необязательны — при их отсутствии берутся типовые. */
static int has_pieces(const config *cfg, const char *section)
{
    return config_get(cfg, section, "corner_tl", NULL) != NULL;
}

static int rect_ok(const ui_rect *r, const plat_image *img)
{
    return r->w > 0 && r->h > 0 &&
           r->x >= 0 && r->y >= 0 &&
           r->x + r->w <= plat_image_width(img) &&
           r->y + r->h <= plat_image_height(img);
}

int ui_init(const char *windows_cfg_path)
{
    ui_shutdown();

    config *cfg = config_load(windows_cfg_path);
    if (!cfg) {
        fprintf(stderr, "ui_init: не найден '%s'\n", windows_cfg_path);
        return -1;
    }

    const char *image_path = config_get(cfg, "skin", "image", "img/windows.png");
    g_skin = plat_load_image(image_path);
    if (!g_skin) {
        config_free(cfg);
        return -1;
    }

    int rc = -1;
    do {
        if (load_pieces(cfg, windows_cfg_path, "button", g_button) != 0)
            break;
        if (load_pieces(cfg, windows_cfg_path, "button_pressed",
                        g_button_pressed) != 0)
            break;
        if (load_pieces(cfg, windows_cfg_path, "window", g_win) != 0)
            break;
        if (load_rect(cfg, windows_cfg_path, "window", "bg_pattern",
                      &g_win_bg) != 0)
            break;
        /* HUD-скин (окно HUD и кнопка "Завершить ход"): секции
         * необязательны — без них используются типовые элементы */
        if (has_pieces(cfg, "hud") &&
            load_pieces(cfg, windows_cfg_path, "hud", g_hud) == 0 &&
            load_rect(cfg, windows_cfg_path, "hud", "bg_pattern",
                      &g_hud_bg) == 0) {
            /* загружено */
        } else {
            memcpy(g_hud, g_win, sizeof(g_hud));
            g_hud_bg = g_win_bg;
        }
        if (has_pieces(cfg, "hud_button") &&
            load_pieces(cfg, windows_cfg_path, "hud_button",
                        g_hud_btn) == 0) {
            /* загружено */
        } else {
            memcpy(g_hud_btn, g_button, sizeof(g_hud_btn));
        }
        if (has_pieces(cfg, "hud_button_pressed") &&
            load_pieces(cfg, windows_cfg_path, "hud_button_pressed",
                        g_hud_btn_pressed) == 0) {
            /* загружено */
        } else {
            memcpy(g_hud_btn_pressed, g_button_pressed,
                   sizeof(g_hud_btn_pressed));
        }
        if (load_rect(cfg, windows_cfg_path, "progressbar", "start",
                      &g_pb_start) != 0 ||
            load_rect(cfg, windows_cfg_path, "progressbar", "end",
                      &g_pb_end) != 0 ||
            load_rect(cfg, windows_cfg_path, "progressbar", "fill",
                      &g_pb_fill) != 0 ||
            load_rect(cfg, windows_cfg_path, "progressbar", "background",
                      &g_pb_track) != 0)
            break;

        const ui_rect *all[] = {
            g_button, g_button_pressed, g_win, g_hud, g_hud_btn,
            g_hud_btn_pressed
        };
        for (int a = 0; a < 6; a++)
            for (int i = 0; i < P_COUNT; i++)
                if (!rect_ok(&all[a][i], g_skin)) {
                    fprintf(stderr, "ui_init: элемент '%s' выходит за границы %s\n",
                            image_path, image_path);
                    goto fail;
                }
        if (!rect_ok(&g_win_bg, g_skin) || !rect_ok(&g_pb_start, g_skin) ||
            !rect_ok(&g_pb_end, g_skin) || !rect_ok(&g_pb_fill, g_skin) ||
            !rect_ok(&g_pb_track, g_skin)) {
            fprintf(stderr, "ui_init: элемент выходит за границы %s\n", image_path);
            goto fail;
        }
        rc = 0;
    } while (0);

fail:
    config_free(cfg);
    if (rc != 0)
        ui_shutdown();
    return rc;
}

void ui_shutdown(void)
{
    plat_free_image(g_skin);
    g_skin = NULL;
}

/* ------------------------------------------------------------ 9-срезы */

/* Растягивает элемент в заданный прямоугольник. */
static void draw_stretch(const ui_rect *r, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    plat_draw_image(g_skin, r->x, r->y, r->w, r->h, x, y, w, h);
}

/* Повторяет элемент по горизонтали, последний экземпляр обрезается
 * по источнику (без искажения текстуры); высота — как у элемента. */
static void draw_tile_h(const ui_rect *r, int x, int y, int w)
{
    for (int dx = 0; dx < w; dx += r->w) {
        int tw = (w - dx < r->w) ? (w - dx) : r->w;
        plat_draw_image(g_skin, r->x, r->y, tw, r->h, x + dx, y, tw, r->h);
    }
}

/* То же по вертикали; ширина — как у элемента. */
static void draw_tile_v(const ui_rect *r, int x, int y, int h)
{
    for (int dy = 0; dy < h; dy += r->h) {
        int th = (h - dy < r->h) ? (h - dy) : r->h;
        plat_draw_image(g_skin, r->x, r->y, r->w, th, x, y + dy, r->w, th);
    }
}

/* Повторяет элемент по обеим осям с обрезкой по источнику. */
static void draw_tile_both(const ui_rect *r, int x, int y, int w, int h)
{
    for (int dy = 0; dy < h; dy += r->h) {
        int th = (h - dy < r->h) ? (h - dy) : r->h;
        for (int dx = 0; dx < w; dx += r->w) {
            int tw = (w - dx < r->w) ? (w - dx) : r->w;
            plat_draw_image(g_skin, r->x, r->y, tw, th, x + dx, y + dy, tw, th);
        }
    }
}

/* Центральная грань: заполняет область МЕЖДУ РЁБРАМИ (а не между углами —
 * ширина/высота рёбер может отличаться от размера углов). */
static void draw_center(const ui_rect *p, int x, int y, int w, int h)
{
    int cw = p[P_TL].w, ch = p[P_TL].h;
    if (w < 2 * cw)
        cw = w / 2; /* очень узкая кнопка: углы сжимаются */
    if (h < 2 * ch)
        ch = h / 2;
    if (cw < 1 || ch < 1)
        return;
    draw_tile_both(&p[P_CENTER],
                   x + p[P_LEFT].w, y + p[P_TOP].h,
                   w - p[P_LEFT].w - p[P_RIGHT].w,
                   h - p[P_TOP].h - p[P_BOTTOM].h);
}

/* Рамка: рёбра между углами, углы — последними, поверх заливки: они
 * непрозрачные и перекрывают её в своих квадрантах, поэтому рамка
 * получается непрерывной при любой толщине рёбер. */
static void draw_border(const ui_rect *p, int x, int y, int w, int h)
{
    int cw = p[P_TL].w, ch = p[P_TL].h;
    if (w < 2 * cw)
        cw = w / 2; /* очень узкая кнопка: углы сжимаются */
    if (h < 2 * ch)
        ch = h / 2;
    if (cw < 1 || ch < 1)
        return;

    draw_tile_h(&p[P_TOP], x + cw, y, w - 2 * cw);
    draw_tile_h(&p[P_BOTTOM], x + cw, y + h - p[P_BOTTOM].h, w - 2 * cw);
    draw_tile_v(&p[P_LEFT], x, y + ch, h - 2 * ch);
    draw_tile_v(&p[P_RIGHT], x + w - p[P_RIGHT].w, y + ch, h - 2 * ch);

    draw_stretch(&p[P_TL], x, y, cw, ch);
    draw_stretch(&p[P_TR], x + w - cw, y, cw, ch);
    draw_stretch(&p[P_BL], x, y + h - ch, cw, ch);
    draw_stretch(&p[P_BR], x + w - cw, y + h - ch, cw, ch);
}

/* Сборка 9-среза: сначала заливка, затем рамка. */
static void draw_9slice(const ui_rect *p, int x, int y, int w, int h)
{
    draw_center(p, x, y, w, h);
    draw_border(p, x, y, w, h);
}

/* ------------------------------------------------------------ кнопки */

/* Надпись или глиф по центру кнопки (общая логика размещения). */
static void draw_button_label(const ui_button *btn, const char *text)
{
    int scr_h = plat_screen_height();
    int len = (int)strlen(text);
    /* базовый размер — 55% высоты кнопки; при необходимости
     * сжимаем, чтобы текст занимал не более 80% ширины */
    float size = 0.55f * (float)btn->h / (float)scr_h;
    float fit_w = 0.8f * (float)btn->w / ((float)len * (float)scr_h);
    if (len > 0 && size > fit_w)
        size = fit_w;
    int glyph = (int)(size * (float)scr_h + 0.5f);
    int text_w = string_width(text, size);
    int tx = btn->x + (btn->w - text_w) / 2;
    int ty = btn->y + (btn->h - glyph) / 2;
    if (btn->pressed) {
        tx += 2; /* надпись «утапливается» вместе с кнопкой */
        ty += 2;
    }
    draw_string(text, tx, ty, size);
}

void ui_button_set_skin(int index, int skin)
{
    if (index < 0 || index >= ui_buttons_count)
        return;
    ui_buttons[index].skin = (skin == UI_SKIN_HUD) ? UI_SKIN_HUD
                                                   : UI_SKIN_DEFAULT;
}

void ui_button_draw(const ui_button *btn)
{
    if (!btn || !btn->enabled || !g_skin)
        return;

    const ui_rect *pieces;
    if (btn->skin == UI_SKIN_HUD)
        pieces = btn->pressed ? g_hud_btn_pressed : g_hud_btn;
    else
        pieces = btn->pressed ? g_button_pressed : g_button;
    draw_9slice(pieces, btn->x, btn->y, btn->w, btn->h);

    /* рисуется надпись, а если она пуста — глиф (крестик и т.п.) */
    if (btn->caption[0] != '\0')
        draw_button_label(btn, btn->caption);
    else if (btn->glyph[0] != '\0')
        draw_button_label(btn, btn->glyph);
}

int ui_button_check_press(const ui_button *btn)
{
    if (!btn || !btn->enabled)
        return 0;
    int mx, my;
    plat_mouse_pos(&mx, &my);
    if (mx < btn->x || mx >= btn->x + btn->w ||
        my < btn->y || my >= btn->y + btn->h)
        return 0;
    return plat_mouse_left_down();
}

int ui_buttons_process(void)
{
    /* фронт нажатия/отпускания читается один раз за кадр для всех кнопок */
    int press = plat_mouse_left_pressed();
    int release = plat_mouse_left_released();
    int down = plat_mouse_left_down();
    return ui_buttons_process_ext(press, release, down);
}

int ui_buttons_process_ext(int left_pressed, int left_released,
                           int left_down)
{
    int press = left_pressed;
    int release = left_released;
    int down = left_down;
    int mx, my;
    int hit = 0; /* нажатие пришлось внутрь отрисовываемой кнопки */
    plat_mouse_pos(&mx, &my);

    for (int i = 0; i < ui_buttons_count; i++) {
        ui_button *b = &ui_buttons[i];
        if (!b->enabled) {
            b->pressed = 0;
            continue;
        }
        int inside = mx >= b->x && mx < b->x + b->w &&
                     my >= b->y && my < b->y + b->h;
        if (press && inside) {
            b->pressed = 1;
            hit = 1;
        }
        else if (b->pressed && down && !inside)
            b->pressed = 0;         /* увели курсор с удерживаемой кнопки */
        if (release) {
            if (b->pressed && inside && b->on_press)
                b->on_press();
            b->pressed = 0;
        }
    }
    return hit;
}

void ui_buttons_clear(void)
{
    ui_buttons_count = 0;
}

void ui_buttons_draw(void)
{
    for (int i = 0; i < ui_buttons_count; i++)
        if (ui_buttons[i].enabled)
            ui_button_draw(&ui_buttons[i]);
}

void ui_button_set_glyph(int index, const char *glyph_1251)
{
    if (index < 0 || index >= ui_buttons_count)
        return;
    ui_button *b = &ui_buttons[index];
    if (glyph_1251) {
        strncpy(b->glyph, glyph_1251, UI_BUTTON_GLYPH_MAX - 1);
        b->glyph[UI_BUTTON_GLYPH_MAX - 1] = '\0';
    } else {
        b->glyph[0] = '\0';
    }
}

/* ------------------------------------------------------------ окно */

void ui_window_draw(int x, int y, int w, int h)
{
    if (!g_skin || w <= 0 || h <= 0)
        return;
    /* заливка между рёбрами, поверх неё фон-паттерн, рамка — последней */
    draw_center(g_win, x, y, w, h);
    draw_tile_both(&g_win_bg,
                   x + g_win[P_LEFT].w, y + g_win[P_TOP].h,
                   w - g_win[P_LEFT].w - g_win[P_RIGHT].w,
                   h - g_win[P_TOP].h - g_win[P_BOTTOM].h);
    draw_border(g_win, x, y, w, h);
}

/* Окно HUD — та же сборка, что у ui_window_draw, но киберпанк-тайлами
 * из секции {hud} (без секции — типовыми, см. ui_init). */
void ui_hud_window_draw(int x, int y, int w, int h)
{
    if (!g_skin || w <= 0 || h <= 0)
        return;
    draw_center(g_hud, x, y, w, h);
    draw_tile_both(&g_hud_bg,
                   x + g_hud[P_LEFT].w, y + g_hud[P_TOP].h,
                   w - g_hud[P_LEFT].w - g_hud[P_RIGHT].w,
                   h - g_hud[P_TOP].h - g_hud[P_BOTTOM].h);
    draw_border(g_hud, x, y, w, h);
}

/* ------------------------------------------------------------ прогрессбар */

/* Полоса из повторяющегося элемента, растянутая по высоте h. */
static void draw_bar_tile(const ui_rect *r, int x, int y, int w, int h)
{
    for (int dx = 0; dx < w; dx += r->w) {
        int tw = (w - dx < r->w) ? (w - dx) : r->w;
        plat_draw_image(g_skin, r->x, r->y, tw, r->h, x + dx, y, tw, h);
    }
}

void ui_progressbar_draw(int x, int y, int w, int h, float fraction)
{
    if (!g_skin || w <= 0 || h <= 0)
        return;
    if (fraction < 0.0f)
        fraction = 0.0f;
    if (fraction > 1.0f)
        fraction = 1.0f;

    draw_bar_tile(&g_pb_track, x, y, w, h);

    int fw = (int)((float)w * fraction + 0.5f);
    int cap = h; /* торцы квадратные, масштабируются по высоте полосы */
    if (fw >= 2 * cap) {
        draw_bar_tile(&g_pb_fill, x + cap, y, fw - 2 * cap, h);
        draw_stretch(&g_pb_start, x, y, cap, h);
        draw_stretch(&g_pb_end, x + fw - cap, y, cap, h);
    } else if (fw > 0) {
        draw_bar_tile(&g_pb_fill, x, y, fw, h);
    }
}
