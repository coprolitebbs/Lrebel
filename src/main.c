/*
 * main.c — Lrebel, этап 2: загрузка с прогрессбаром и главное меню.
 *
 * При старте последовательно загружаются конфигурационные файлы и
 * изображения (config/main.cfg, img/font.png, config/windows.cfg,
 * img/windows.png); каждый шаг обновляет прогрессбар. После загрузки
 * отрисовывается главное меню: типовое окно на весь экран как фон, строка
 * title (секция {main}, параметр title) вверху экрана и внизу две кнопки —
 * «Новая игра» (секция {main_btns}, параметр play_button, обработчик пока
 * не привязан) и «Выход» ({main_btns}, close_button, обработчик app_quit
 * завершает приложение). В правом верхнем углу окна — кнопка закрытия
 * с глифом-крестиком ({main_btns}, close_glyph) вместо надписи и той же
 * функцией app_quit. Корректно завершается по закрытию окна и по кнопкам.
 *
 * Верхний уровень: main.c, ui.c, font.c, readconfig.c — только
 * абстрактные функции portable_posix.h и ui.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portable_posix.h"
#include "readconfig.h"
#include "utf8_win1251.h"
#include "font.h"
#include "ui.h"

#define CONFIG_PATH   "config/main.cfg"
#define WINDOWS_CFG   "config/windows.cfg"
#define FONT_PATH     "img/font.png"
#define WINDOW_TITLE  "Lrebel"

#define DEFAULT_WIN_W 1024
#define DEFAULT_WIN_H 768

/* --- строки меню (Windows-1251); живут всё время работы меню --- */
static char  title_1251[256];
static float title_size = 0.2f;

/* типовая функция завершения приложения; привязывается к кнопке «Выход» */
static int g_app_quit = 0;
static void app_quit(void)
{
    g_app_quit = 1;
}

/* подобрать размер символа title, чтобы строка поместилась на экране */
static float fit_title_size(const char *str)
{
    float size = 0.2f;
    int len = (int)strlen(str);
    int scr_w = plat_screen_width();
    int scr_h = plat_screen_height();
    if (len > 0 && scr_w > 0 && scr_h > 0) {
        float fit = 0.9f * (float)scr_w / ((float)len * (float)scr_h);
        if (size > fit)
            size = fit;
    }
    return size;
}

/* кадр экрана загрузки: title вверху и прогрессбар */
static void draw_loading_frame(float fraction)
{
    plat_clear_screen(0, 0, 0);

    int scr_w = plat_screen_width();
    int scr_h = plat_screen_height();

    if (title_1251[0] != '\0') {
        int glyph = (int)(title_size * (float)scr_h + 0.5f);
        int text_w = string_width(title_1251, title_size);
        draw_string(title_1251, (scr_w - text_w) / 2, glyph / 2, title_size);
    }

    if (fraction >= 0.0f) {
        int bar_w = scr_w * 6 / 10;
        int bar_h = scr_h / 16;
        ui_progressbar_draw((scr_w - bar_w) / 2, scr_h * 2 / 3,
                            bar_w, bar_h, fraction);
    }

    plat_flip_frame();
}

/* кадр главного меню: окно на весь экран как фон, поверх — title вверху
 * и кнопки внизу */
static void draw_menu_frame(void)
{
    plat_clear_screen(0, 0, 0);

    int scr_w = plat_screen_width();
    int scr_h = plat_screen_height();

    /* типовое окно на весь экран — задний план главного меню */
    ui_window_draw(0, 0, scr_w, scr_h);

    int glyph = (int)(title_size * (float)scr_h + 0.5f);
    int text_w = string_width(title_1251, title_size);
    draw_string(title_1251, (scr_w - text_w) / 2, glyph / 2, title_size);

    ui_buttons_draw();
    plat_flip_frame();
}

/* добавить две кнопки главного меню и кнопку закрытия окна (крестик) */
static void add_menu_buttons(const config *cfg)
{
    const char *play_utf8  = config_get(cfg, "main_btns", "play_button",  "");
    const char *close_utf8 = config_get(cfg, "main_btns", "close_button", "");
    const char *x_utf8     = config_get(cfg, "main_btns", "close_glyph",  "X");

    char play_1251[UI_BUTTON_CAPTION_MAX];
    char close_1251[UI_BUTTON_CAPTION_MAX];
    char x_1251[UI_BUTTON_GLYPH_MAX];
    utf8_to_win1251(play_utf8,  play_1251,  sizeof(play_1251));
    utf8_to_win1251(close_utf8, close_1251, sizeof(close_1251));
    utf8_to_win1251(x_utf8,     x_1251,     sizeof(x_1251));

    int scr_w = plat_screen_width();
    int scr_h = plat_screen_height();
    int bh = scr_h / 8;          /* высота кнопки */
    int bw = scr_w * 3 / 10;     /* ширина кнопки */
    int gap = bh / 2;            /* промежуток между кнопками */
    int x0 = (scr_w - (bw * 2 + gap)) / 2;
    int y  = scr_h - bh - bh / 2;

    /* «Новая игра»: обработчик пока не привязан (NULL) */
    ui_button_add(x0, y, bw, bh, play_1251, NULL);
    /* «Выход»: завершение приложения */
    ui_button_add(x0 + bw + gap, y, bw, bh, close_1251, app_quit);

    /* кнопка закрытия окна: пустой caption, глиф-крестик вместо него и
     * та же функция завершения, что у кнопки «Выход» */
    int csz = bh * 2 / 3;        /* квадратная кнопка в правом верхнем углу */
    int margin = csz / 2;
    int idx = ui_button_add(scr_w - csz - margin, margin, csz, csz, "", app_quit);
    ui_button_set_glyph(idx, x_1251);
}

int main(void)
{
    /* --- шаг 1: конфигурация --- */
    config *cfg = config_load(CONFIG_PATH);
    if (!cfg) {
        fprintf(stderr, "main: не найден '%s' (запускать из корня репозитория)\n",
                CONFIG_PATH);
        return 1;
    }

    int win_w = config_get_int(cfg, "main", "width",  DEFAULT_WIN_W);
    int win_h = config_get_int(cfg, "main", "height", DEFAULT_WIN_H);
    int load_delay = config_get_int(cfg, "main", "load_step_delay_ms", 150);

    /* --- шаг 2: платформа --- */
    if (plat_init(win_w, win_h, WINDOW_TITLE) != 0) {
        config_free(cfg);
        return 1;
    }
    plat_clear_screen(0, 0, 0);  /* окно сразу заливается чёрным */
    plat_flip_frame();

    /* --- шаг 3: шрифт (chartable + img/font.png) --- */
    const char *chartable_utf8 = config_get(cfg, "font", "chartable", "");
    char chartable_1251[512];
    if (utf8_to_win1251(chartable_utf8, chartable_1251,
                        sizeof(chartable_1251)) < 0) {
        fprintf(stderr, "main: буфер chartable слишком мал\n");
        plat_shutdown();
        config_free(cfg);
        return 1;
    }
    if (font_init(chartable_1251, FONT_PATH) != 0) {
        plat_shutdown();
        config_free(cfg);
        return 1;
    }

    /* --- шаг 4: title (секция {main}) --- */
    const char *title_utf8 = config_get(cfg, "main", "title", "");
    if (utf8_to_win1251(title_utf8, title_1251, sizeof(title_1251)) < 0) {
        fprintf(stderr, "main: буфер title слишком мал\n");
        font_shutdown();
        plat_shutdown();
        config_free(cfg);
        return 1;
    }
    title_size = fit_title_size(title_1251);
    draw_loading_frame(-1.0f);   /* шрифт готов: кадр с title */
    plat_delay_ms(load_delay);

    /* --- шаг 5: скин интерфейса (config/windows.cfg + img/windows.png) --- */
    if (ui_init(WINDOWS_CFG) != 0) {
        font_shutdown();
        plat_shutdown();
        config_free(cfg);
        return 1;
    }
    draw_loading_frame(0.6f);
    plat_delay_ms(load_delay);

    /* --- шаг 6: кнопки меню (секция {main_btns}) --- */
    add_menu_buttons(cfg);
    draw_loading_frame(1.0f);
    plat_delay_ms(load_delay);

    /* --- главный цикл меню --- */
    while (!plat_poll_events() && !g_app_quit) {
        ui_buttons_process();     /* мышь -> состояния и обработчики кнопок */
        draw_menu_frame();
    }

    /* --- корректное завершение --- */
    ui_shutdown();
    font_shutdown();
    plat_shutdown();
    config_free(cfg);
    return 0;
}
