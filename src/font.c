/*
 * font.c — отрисовка строк шрифтом img/font.png.
 * Графика и экран — только через платформенный уровень (plat_*).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portable_posix.h"
#include "font.h"

/* тайлов в строке изображения — по формуле Y = number / 16 из ТЗ */
#define FONT_COLS 16

static plat_image *g_font_img = NULL;
static int g_tile     = 0; /* размер тайла в пикселях (64 для 1024/16) */
static int g_img_rows = 0; /* число рядов тайлов в изображении */
static int g_lookup[256];  /* номер символа в chartable; -1 — символа нет */

int font_init(const char *chartable_win1251, const char *font_image_path)
{
    font_shutdown();

    if (!chartable_win1251 || *chartable_win1251 == '\0') {
        fprintf(stderr, "font_init: пустая chartable\n");
        return -1;
    }

    g_font_img = plat_load_image(font_image_path);
    if (!g_font_img)
        return -1;

    int img_w = plat_image_width(g_font_img);
    int img_h = plat_image_height(g_font_img);

    if (img_w % FONT_COLS != 0) {
        fprintf(stderr, "font_init: '%s': ширина %d не кратна %d тайлам\n",
                font_image_path, img_w, FONT_COLS);
        font_shutdown();
        return -1;
    }
    g_tile = img_w / FONT_COLS;
    if (g_tile <= 0 || img_h % g_tile != 0) {
        fprintf(stderr, "font_init: '%s': высота %d не кратна тайлу %d\n",
                font_image_path, img_h, g_tile);
        font_shutdown();
        return -1;
    }
    g_img_rows = img_h / g_tile;

    int chars = (int)strlen(chartable_win1251);
    if (chars > FONT_COLS * g_img_rows)
        fprintf(stderr,
                "font_init: предупреждение: символов в chartable (%d) больше, "
                "чем тайлов в изображении (%d x %d)\n",
                chars, FONT_COLS, g_img_rows);

    for (int i = 0; i < 256; i++)
        g_lookup[i] = -1;
    for (int i = 0; i < chars; i++) {
        unsigned char c = (unsigned char)chartable_win1251[i];
        if (c == '\0')
            break;
        if (g_lookup[c] < 0)
            g_lookup[c] = i; /* первое вхождение в chartable */
    }

    return 0;
}

void font_shutdown(void)
{
    plat_free_image(g_font_img);
    g_font_img = NULL;
    g_tile = 0;
    g_img_rows = 0;
}

/* Размер символа в пикселях: size — относительная величина, 1.0f — экран. */
static int glyph_pixels(float size)
{
    int px = (int)(size * (float)plat_screen_height() + 0.5f);
    return px > 0 ? px : 0;
}

void draw_string(const char *str_to_print, int x, int y, float size)
{
    if (!g_font_img || !str_to_print)
        return;

    int glyph = glyph_pixels(size);
    if (glyph <= 0)
        return;

    for (const unsigned char *p = (const unsigned char *)str_to_print; *p; p++) {
        int number = g_lookup[*p];
        if (number >= 0) {
            /* номер символа -> координаты тайла (ряды считаются снизу):
             *   Y = (int)(number / 16); X = number - (Y * 16);            */
            int ty = (int)(number / FONT_COLS);
            int tx = number - ty * FONT_COLS;
            if (ty < g_img_rows) {
                int src_x = tx * g_tile;
                int src_y = (g_img_rows - 1 - ty) * g_tile; /* снизу -> в PNG */
                plat_draw_image(g_font_img,
                                src_x, src_y, g_tile, g_tile,
                                x, y, glyph, glyph);
            }
        }
        x += glyph;
    }
}

int string_width(const char *str_to_print, float size)
{
    if (!str_to_print)
        return 0;
    int glyph = glyph_pixels(size);
    return (int)strlen(str_to_print) * glyph;
}
