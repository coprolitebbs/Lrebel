/*
 * test_editor_inv.c — смоук-тест редактора и окна инвентаря без реального
 * окна: SDL_VIDEODRIVER=dummy, синтетические события клавиатуры/мыши и
 * чтение пикселей из бэкбуфера рендера (доступ к g_renderer через
 * трюк "#define static" — см. portable_posix.c и editor.c в конец файла).
 *
 * Проверяются:
 *   - режим объектов: ЛКМ ставит объект и делает его "текущим выбранным";
 *     ПКМ выбирает объект / сбрасывает выбор на пустой ячейке;
 *   - DEL удаляет выбранный объект и сбрасывает выбор; инвентари
 *     объектов корректно сдвигаются вместе с объектами;
 *   - P открывает/закрывает окно инвентаря в левой половине окна
 *     (пиксельные проверки сетки и слота оружия);
 *   - O открывает окно выбора предмета; клик добавляет предмет в
 *     инвентарь выбранного объекта (с габаритами из config/objects.cfg);
 *   - перетаскивание предметов мышью: перенос в свободную ячейку,
 *     выброс за пределы инвентаря, назначение основного оружия.
 *
 * Сборка (из корня репозитория):
 *   cc -std=c99 -O2 -Wall -I<SDL2 inc> -I<x86 inc> -Isrc \
 *      tests/test_editor_inv.c src/inventory.c src/inventory_ui.c \
 *      src/objects.c src/map.c src/font.c src/ui.c src/readconfig.c \
 *      src/utf8_win1251.c -o build/test_editor_inv -l:libSDL2-2.0.so.0 -lm
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
#include "editor.h"

/* --- трюк: открыть static-определения двух модулей в этот TU --- */
#define static
#include "portable_posix.c" /* доступ к g_renderer (чтение пикселей)      */
#include "editor.c"         /* доступ к g_map/g_selected/g_obj_inv/g_modal */
#undef static

#define TEST_MAP_PATH "/tmp/lrebel_invtest.map"

/* ------------------------------------------------------------- помощники */

/* один кадр редактора: разобрать события, обработать ввод и нарисовать */
static void frame(void)
{
    plat_poll_events();
    editor_frame(0.016f);
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

static void click_right(int x, int y)
{
    push_mouse_motion(x, y);
    push_mouse_button(SDL_BUTTON_RIGHT, 1, x, y);
    push_mouse_button(SDL_BUTTON_RIGHT, 0, x, y);
    frame();
}

/* перетаскивание левой кнопкой за 3 кадра: нажать, перенести, отпустить */
static void drag_left(int x0, int y0, int x1, int y1)
{
    push_mouse_motion(x0, y0);
    push_mouse_button(SDL_BUTTON_LEFT, 1, x0, y0);
    frame();
    push_mouse_motion(x1, y1);
    frame();
    push_mouse_button(SDL_BUTTON_LEFT, 0, x1, y1);
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

static int px_diff(int x, int y, unsigned char r, unsigned char g,
                   unsigned char b)
{
    unsigned char pr, pg, pb;
    read_px(x, y, &pr, &pg, &pb);
    return pr != r || pg != g || pb != b;
}

/* экранная точка центра ячейки карты (камера в центре карты 64x64,
 * видимых элементов 32, тайл на экране 24px) */
static int cell_x(int cx) { return (int)(((float)cx + 0.5f - 32.0f) * 24.0f) + 512; }
static int cell_y(int cy) { return (int)(((float)cy + 0.5f - 32.0f) * 24.0f) + 384; }

int main(void)
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    /* --- тестовая карта 64x64 с поверхностью --- */
    game_map tm;
    assert(map_init(&tm, 64, 64) == 0);
    for (int i = 0; i < 64 * 64; i++)
        tm.layers[0][i] = (unsigned short)MAP_TILE_ID(MAP_SURFACE_ROW, 1);
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
    assert(editor_init(TEST_MAP_PATH) == 0);
    frame();

    printf("== 1. режим объектов: ЛКМ ставит объект и выбирает его ==\n");
    push_key('2', SDL_SCANCODE_2);
    frame();
    assert(g_mode == ED_MODE_OBJECTS);
    assert(g_map.object_count == 0);
    click_left(cell_x(32), cell_y(32));
    assert(g_map.object_count == 1);
    assert(g_selected == 0); /* добавленный объект стал выбранным */
    assert((int)g_map.objects[0].x == 32 && (int)g_map.objects[0].y == 32);

    printf("== 2. ПКМ выбирает объект / пустая ячейка сбрасывает ==\n");
    click_left(cell_x(33), cell_y(32));   /* второй объект */
    assert(g_map.object_count == 2 && g_selected == 1);
    click_right(cell_x(32), cell_y(32));  /* выбор первого объекта */
    assert(g_selected == 0);
    click_right(cell_x(35), cell_y(35));  /* пустая ячейка */
    assert(g_selected == -1);
    click_right(cell_x(33), cell_y(32));  /* снова выбираем второй */
    assert(g_selected == 1);

    printf("== 3. P открывает окно инвентаря (левая половина) ==\n");
    unsigned char r0, g0, b0;
    read_px(500, 700, &r0, &g0, &b0); /* фон карты до открытия окна */
    push_key('p', SDL_SCANCODE_P);
    frame();
    assert(inventory_ui_is_open());
    assert(g_obj_inv[1] != NULL); /* инвентарь выбранного объекта создан */
    /* фон в левой половине сменился на скин окна */
    assert(px_diff(500, 700, r0, g0, b0));
    /* рамка ячейки (14,14) сетки инвентаря — цвет (80,80,100) */
    assert(px_diff(464, 488, 0, 0, 0) && !px_diff(464, 488, 80, 80, 100));
    /* рамка слота основного оружия — цвет (150,150,175) */
    assert(!px_diff(16, 532, 150, 150, 175));

    printf("== 4. O: клик добавляет предмет (пистолет 1x2) ==\n");
    push_key('o', SDL_SCANCODE_O);
    frame();
    assert(g_modal == ED_MODAL_INVPICK);
    click_left(200, 382); /* первый тайл окна выбора — {object1}, 1x2 */
    {
        inventory *inv = g_obj_inv[1];
        assert(inventory_used_count(inv) == 1);
        inventory_item *it = inventory_item_at(inv, 0, 0);
        assert(it && it->proto);
        assert(it->ammo_loaded == 8); /* магазин из config/objects.cfg */
        int sx, sy;
        inventory_item_size(it, &sx, &sy);
        assert(sx == 1 && sy == 2);
        assert(inventory_item_at(inv, 0, 1) == it);
    }

    printf("== 5. O: боеприпас (кол 11) добавляется рядом, стек 30 ==\n");
    click_left(640, 382); /* колонка 11 окна выбора — {object12} */
    {
        inventory *inv = g_obj_inv[1];
        assert(inventory_used_count(inv) == 2);
        inventory_item *it = inventory_item_at(inv, 1, 0);
        assert(it && it->proto && it->count == 30);
        int sx, sy;
        inventory_item_size(it, &sx, &sy);
        assert(sx == 1 && sy == 1); /* боеприпас 1x1 */
    }
    push_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE); /* закрыть окно выбора */
    frame();
    assert(g_modal == ED_MODAL_NONE);
    assert(inventory_ui_is_open()); /* окно инвентаря осталось открытым */

    printf("== 6. перетаскивание: боеприпас (1,0) -> (10,10) ==\n");
    {
        inventory *inv = g_obj_inv[1];
        inventory_item *ammo = inventory_item_at(inv, 1, 0);
        drag_left(64, 56, 352, 376); /* центр ячейки (1,0) -> (10,10) */
        assert(inventory_item_at(inv, 10, 10) == ammo);
        assert(inventory_item_at(inv, 1, 0) == NULL);
    }

    printf("== 7. выброс предмета за пределы инвентаря ==\n");
    {
        inventory *inv = g_obj_inv[1];
        assert(inventory_used_count(inv) == 2);
        drag_left(352, 376, 900, 500); /* правая половина окна — мимо */
        assert(inventory_used_count(inv) == 1);
        assert(inventory_item_at(inv, 10, 10) == NULL);
    }

    printf("== 8. пистолет в слот основного оружия ==\n");
    {
        inventory *inv = g_obj_inv[1];
        inventory_item *w = inventory_item_at(inv, 0, 0);
        assert(w);
        drag_left(32, 56, 256, 576); /* ячейка (0,0) -> область оружия */
        assert(inv->selected_weapon == w);
        assert(inventory_item_at(inv, 0, 0) == w); /* остался в сетке */
        assert(w->ammo_loaded == 8);
    }

    printf("== 9. повторное P закрывает окно ==\n");
    push_key('p', SDL_SCANCODE_P);
    frame();
    assert(!inventory_ui_is_open());

    printf("== 10. DEL удаляет выбранный объект; выбор сброшен ==\n");
    click_right(cell_x(32), cell_y(32)); /* выбрать объект 0 */
    assert(g_selected == 0);
    push_key(SDLK_DELETE, SDL_SCANCODE_DELETE);
    frame();
    assert(g_map.object_count == 1);
    assert(g_selected == -1);
    /* инвентарь удаленного объекта освобожден и удален со сдвигом:
     * на месте 0 — инвентарь выжившего объекта (бывшего N1) */
    assert(g_obj_inv[1] == NULL);
    assert(g_obj_inv[0] != NULL);
    /* оставшийся объект (бывший N1) сместился на индекс 0 */
    assert((int)g_map.objects[0].x == 33);

    printf("== 11. инвентарь сдвинутого объекта доступен по новому индексу ==\n");
    click_right(cell_x(33), cell_y(32));
    assert(g_selected == 0);
    push_key('p', SDL_SCANCODE_P);
    frame();
    assert(inventory_ui_is_open());
    assert(g_obj_inv[0] != NULL);
    /* у выжившего объекта его инвентарь с пистолетом (выбросил только
     * боеприпас) — указатель на оружие сохранился */
    assert(inventory_used_count(g_obj_inv[0]) == 1);
    assert(g_obj_inv[0]->selected_weapon != NULL);
    push_key('p', SDL_SCANCODE_P);
    frame();
    assert(!inventory_ui_is_open());

    printf("== 12. DEL последнего объекта ==\n");
    push_key(SDLK_DELETE, SDL_SCANCODE_DELETE);
    frame();
    assert(g_map.object_count == 0);
    assert(g_obj_inv[0] == NULL);

    editor_shutdown();
    ui_shutdown();
    font_shutdown();
    plat_shutdown();
    remove(TEST_MAP_PATH);

    printf("test_editor_inv: все проверки пройдены\n");
    return 0;
}
