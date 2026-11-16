/*
 * main.c — Lrebel, этап 1.
 *
 * Загружает config/main.cfg, конвертирует chartable и title из UTF-8
 * в Windows-1251, загружает шрифт img/font.png и в главном цикле
 * отрисовывает строку title по центру экрана. Корректно завершается
 * по событию закрытия окна.
 *
 * Верхний уровень: здесь и в font.c/readconfig.c — только абстрактные
 * функции платформенного уровня portable_posix.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portable_posix.h"
#include "readconfig.h"
#include "utf8_win1251.h"
#include "font.h"

#define CONFIG_PATH "config/main.cfg"
#define FONT_PATH   "img/font.png"


#define WINDOW_TITLE  "Lrebel"

int main(void)
{
    /* --- конфигурация --- */
    config *cfg = config_load(CONFIG_PATH);
    if (!cfg) {
        fprintf(stderr, "main: не найден '%s' (запускать из корня репозитория)\n",
                CONFIG_PATH);
        return 1;
    }

    int win_w = config_get_int(cfg, "main", "width",  DEFAULT_WIN_W);
    int win_h = config_get_int(cfg, "main", "height", DEFAULT_WIN_H);

    if (plat_init(win_w, win_h, WINDOW_TITLE) != 0) {
        config_free(cfg);
        return 1;
    }

    /* --- chartable: UTF-8 -> Windows-1251 (1 символ = 1 байт) --- */
    const char *chartable_utf8 = config_get(cfg, "font", "chartable", "");
    char chartable_1251[512];
    if (utf8_to_win1251(chartable_utf8, chartable_1251, sizeof(chartable_1251)) < 0) {
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

    /* --- title: UTF-8 -> Windows-1251 --- */
    const char *title_utf8 = config_get(cfg, "main", "title", "");
    char title_1251[256];
    if (utf8_to_win1251(title_utf8, title_1251, sizeof(title_1251)) < 0) {
        fprintf(stderr, "main: буфер title слишком мал\n");
        font_shutdown();
        plat_shutdown();
        config_free(cfg);
        return 1;
    }

    /* относительный размер символа подбирается так, чтобы строка
     * поместилась на экране (максимум 0.2 высоты экрана на символ) */
    float title_size = config_get_float(cfg, "main", "title_size", 0.0f);
    if (title_size <= 0.0f) {
        title_size = 0.2f;
        int len = (int)strlen(title_1251);
        int scr_w = plat_screen_width();
        int scr_h = plat_screen_height();
        if (len > 0 && scr_w > 0) {
            float fit = 0.9f * (float)scr_w / ((float)len * (float)scr_h);
            if (title_size > fit)
                title_size = fit;
        }
    }

    /* --- главный цикл --- */
    while (!plat_poll_events()) {           /* 1 = запрос на закрытие окна */
        plat_clear_screen(0, 0, 0);

        int scr_w = plat_screen_width();
        int scr_h = plat_screen_height();
        int glyph = (int)(title_size * (float)scr_h + 0.5f);
        int text_w = string_width(title_1251, title_size);
        draw_string(title_1251,
                    (scr_w - text_w) / 2,   /* по центру экрана */
                    (scr_h - glyph) / 2,
                    title_size);

        plat_flip_frame();
    }

    /* --- корректное завершение --- */
    font_shutdown();
    plat_shutdown();
    config_free(cfg);
    return 0;
}
