/*
 * test_game_hud.c — смоук-тест панели HUD и кнопки "Завершить ход"
 * в игровом режиме без реального окна: SDL_VIDEODRIVER=dummy,
 * синтетические события клавиатуры/мыши и чтение пикселей из бэкбуфера
 * рендера (доступ к g_renderer через трюк "#define static" — см.
 * portable_posix.c и game.c в конец файла).
 *
 * Проверяются:
 *   - панель HUD рисуется внизу экрана (20% высоты) киберпанк-скином
 *     {hud} из config/windows.cfg: неоново-циановая рамка сверху/снизу
 *     и по бокам, прозрачный скошенный угол (угол панели "срезан"),
 *     статусные строки (ход/юниты/подсказка) внутри панели;
 *   - кнопка "Завершить ход" в правом нижнем углу панели: рамка-скин
 *     {hud_button} и надпись; клик по ней не выбирает объект под
 *     кнопкой и не приводит к действиям (механика ходов будет позже);
 *   - выбор объекта мышью продолжает работать после клика по кнопке
 *     HUD (клик по пустой раскрытой ячейке снимает выбор);
 *   - окно описания (P) пересоздает кнопку HUD (общий массив кнопок
 *     очищается), крестик закрывает окно, кнопка HUD восстанавливается.
 *
 * Сборка (из корня репозитория):
 *   cc -std=c99 -O2 -Wall -I<SDL2 inc> -I<x86 inc> -Isrc \
 *      tests/test_game_hud.c src/inventory.c src/inventory_ui.c \
 *      src/objects.c src/map.c src/font.c src/ui.c src/readconfig.c \
 *      src/utf8_win1251.c -o build/test_game_hud -l:libSDL2-2.0.so.0 -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <SDL.h>

#include "readconfig.h"
#include "utf8_win1251.h"
#include "font.h"
#include "ui.h"
#include "objects.h"
#include "map.h"
#include "inventory.h"
#include "inventory_ui.h"
#include "game.h"

/* --- трюк: открыть static-определения двух модулей в этот TU --- */
#define static
#include "portable_posix.c" /* доступ к g_renderer (чтение пикселей)      */
#include "game.c"           /* доступ к g_selected/g_quit/g_btn_end_turn  */
#undef static

#define TEST_MAP_PATH "/tmp/lrebel_hudtest.map"

/* ------------------------------------------------------------- помощники */

/* один кадр игры: разобрать события, обработать ввод и нарисовать */
static void frame(void)
{
    plat_poll_events();
    game_frame(0.016f);
}

static void push_key(int sym, int scancode)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    ev.key.repeat = 0;
    SDL_PushEvent(&ev);
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYUP;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    SDL_PushEvent(&ev);
}

static void push_mouse_motion(int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_MOUSEMOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    SDL_PushEvent(&ev);
}

static void push_mouse_button(int button, int down, int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.button = (Uint8)button;
    ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.button.clicks = 1;
    ev.button.x = x;
    ev.button.y = y;
    SDL_PushEvent(&ev);
}

/* клик левой кнопкой в точке (движение + нажатие + отпускание за 1 кадр) */
static void click_left(int x, int y)
{
    push_mouse_motion(x, y);
    push_mouse_button(SDL_BUTTON_LEFT, 1, x, y);
    push_mouse_button(SDL_BUTTON_LEFT, 0, x, y);
    frame();
}

static void read_px(int x, int y, unsigned char *r, unsigned char *g,
                    unsigned char *b)
{
    unsigned char px[4];
    SDL_Rect rect = { x, y, 1, 1 };
    if (SDL_RenderReadPixels(g_renderer, &rect, SDL_PIXELFORMAT_RGBA32,
                             px, 4) != 0) {
        fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
        exit(1);
    }
    *r = px[0];
    *g = px[1];
    *b = px[2];
}

static int is_cyan(unsigned char r, unsigned char g, unsigned char b)
{
    return r < 130 && g > 150 && b > 170;
}

static int is_light(unsigned char r, unsigned char g, unsigned char b)
{
    return r > 150 && g > 150 && b > 150;
}

/* число точек с предикатом в прямоугольнике [x0..x1) x [y0..y1) */
static int count_px(int x0, int y0, int x1, int y1,
                    int (*pred)(unsigned char, unsigned char, unsigned char))
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            unsigned char r, g, b;
            read_px(x, y, &r, &g, &b);
            if (pred(r, g, b))
                n++;
        }
    return n;
}

int main(void)
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    /* --- тестовая карта 64x64 с поверхностью и юнитом игрока --- */
    game_map tm;
    assert(map_init(&tm, 64, 64) == 0);
    for (int i = 0; i < 64 * 64; i++)
        tm.layers[0][i] = (unsigned short)MAP_TILE_ID(MAP_SURFACE_ROW, 1);
    /* юнит игрока ("Спецназ-9") в центре — камера стартует на нем */
    assert(map_object_add(&tm, OBJ_TYPE_CHAR_FIRST + CHAR_FACTION_SPETSNAZ,
                          30.0f, 30.0f, 0) >= 0);
    assert(map_save(&tm, TEST_MAP_PATH) == 0);
    map_free(&tm);

    /* --- стартовая загрузка (как в main.c) --- */
    config *cfg = config_load("config/main.cfg");
    assert(cfg);
    char chartable[512];
    assert(utf8_to_win1251(config_get(cfg, "font", "chartable", ""),
                           chartable, sizeof(chartable)) >= 0);
    config_free(cfg);
    assert(objects_load("config/objects.cfg") == 0);
    assert(plat_init(1024, 768, "test") == 0);
    assert(font_init(chartable, "img/font.png") == 0);
    assert(ui_init("config/windows.cfg") == 0);
    assert(game_init(TEST_MAP_PATH) == 0);
    frame();

    /* экран 1024x768: панель HUD — нижние 20% (153px), y=615..767 */
    const int hy = 768 - 768 / 5;
    /* кнопка: ширина 1024/4=256, высота 153*3/8=57, отступы от края */
    const int bw = 1024 / 4, bh = (768 / 5) * 3 / 8;
    const int bx = 1024 - bw - (768 / 5) / 8;
    const int by = 768 - bh - (768 / 5) / 16;

    printf("== 1. панель HUD: киберпанк-рамка по периметру ==\n");
    assert(count_px(100, hy, 900, hy + 1, is_cyan) > 400);   /* верх  */
    assert(count_px(100, 767, 900, 768, is_cyan) > 400);     /* низ   */
    assert(count_px(0, 640, 1, 700, is_cyan) > 30);          /* лево  */
    assert(count_px(1023, 640, 1024, 700, is_cyan) > 30);    /* право */

    printf("== 2. скошенный угол панели (прозрачный угловой треугольник) ==\n");
    {
        unsigned char r, g, b;
        read_px(0, hy, &r, &g, &b);            /* самый угол — скос       */
        assert(!is_cyan(r, g, b));
        read_px(2, hy + 10, &r, &g, &b);       /* диагональ скоса (x+y=12)*/
        assert(is_cyan(r, g, b));
    }

    printf("== 3. статусные строки внутри панели HUD ==\n");
    assert(count_px(8, hy + 2, 520, hy + 88, is_light) > 60);

    printf("== 4. кнопка \"Завершить ход\": рамка и надпись ==\n");
    assert(count_px(bx + 10, by, bx + bw - 10, by + 1, is_cyan) > 100);
    assert(count_px(bx + 10, by + bh - 1, bx + bw - 10, by + bh, is_cyan)
           > 100);
    assert(count_px(bx + 8, by + 10, bx + bw - 8, by + bh - 8, is_light)
           > 25);

    printf("== 5. клик по кнопке ничего не делает и не снимает выбор ==\n");
    click_left(512, 384); /* юнит игрока в центре экрана (камера на нем) */
    assert(g_selected == 0);
    click_left(bx + bw / 2, by + bh / 2);
    assert(g_selected == 0);               /* выбор не снят кнопкой      */
    assert(g_quit == 0);
    assert(g_btn_end_turn >= 0 && g_btn_end_turn < ui_buttons_count);
    assert(ui_buttons[g_btn_end_turn].skin == UI_SKIN_HUD);
    assert(ui_buttons[g_btn_end_turn].pressed == 0); /* состояние сброшено */

    printf("== 6. обычный выбор мышью работает после клика по кнопке ==\n");
    click_left(560, 384); /* пустая раскрытая ячейка рядом — выбор снят  */
    assert(g_selected == -1);
    click_left(512, 384);
    assert(g_selected == 0);

    printf("== 7. P: окно описания пересоздает кнопку HUD ==\n");
    push_key('p', SDL_SCANCODE_P);
    frame();
    assert(g_info_open == 1);
    assert(g_btn_end_turn >= 0 && g_btn_end_turn < ui_buttons_count);
    /* крестик окна описания в правом верхнем углу */
    click_left(1024 - 44 - 12 + 22, 12 + 22);
    assert(g_info_open == 0);
    assert(g_btn_end_turn >= 0 && g_btn_end_turn < ui_buttons_count);
    click_left(bx + bw / 2, by + bh / 2);
    assert(g_selected == 0);               /* кнопка работает и дальше   */

    printf("== 8. несколько кадров без ввода — игра стабильна ==\n");
    for (int k = 0; k < 30; k++)
        frame();
    assert(g_quit == 0);

    game_shutdown();
    printf("test_game_hud: все проверки пройдены\n");
    return 0;
}
