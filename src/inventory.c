/*
 * inventory.c — инвентарь персонажа: см. inventory.h.
 *
 * Логика без графики и платформы: сетка 15x15 указателей на предметы из
 * items_arr, поиск свободного места, добавление, перестановка
 * (перетаскивание мышью) и удаление предметов.
 */
#include <string.h>

#include "inventory.h"

/* ------------------------------------------------------------- утилиты */

void inventory_init(inventory *inv)
{
    if (!inv)
        return;
    memset(inv, 0, sizeof(*inv));
}

int inventory_used_count(const inventory *inv)
{
    if (!inv)
        return 0;
    int n = 0;
    for (int i = 0; i < INVENTORY_ITEMS_MAX; i++)
        if (inv->items_arr[i].proto)
            n++;
    return n;
}

void inventory_item_size(const inventory_item *it, int *sx, int *sy)
{
    if (!it || !it->proto) {
        if (sx) *sx = 0;
        if (sy) *sy = 0;
        return;
    }
    int w = it->proto->inventory_size_x;
    int h = it->proto->inventory_size_y;
    if (it->rotated) {
        int t = w;
        w = h;
        h = t;
    }
    if (sx) *sx = w;
    if (sy) *sy = h;
}

inventory_item *inventory_item_at(const inventory *inv, int col, int row)
{
    if (!inv || col < 0 || row < 0 ||
        col >= INVENTORY_SIZE || row >= INVENTORY_SIZE)
        return NULL;
    return inv->items_places[row][col];
}

/* ------------------------------------------------------- место в сетке */

int inventory_fits(const inventory *inv, int col, int row,
                   int sx, int sy, const inventory_item *except)
{
    if (!inv || sx < 1 || sy < 1)
        return 0;
    if (col < 0 || row < 0 ||
        col + sx > INVENTORY_SIZE || row + sy > INVENTORY_SIZE)
        return 0; /* выходит за пределы сетки */
    for (int r = row; r < row + sy; r++)
        for (int c = col; c < col + sx; c++) {
            inventory_item *p = inv->items_places[r][c];
            if (p && p != except)
                return 0; /* ячейка занята другим предметом */
        }
    return 1;
}

int inventory_find_space(const inventory *inv, int sx, int sy,
                         int *col, int *row)
{
    if (!inv)
        return -1;
    for (int r = 0; r + sy <= INVENTORY_SIZE; r++)
        for (int c = 0; c + sx <= INVENTORY_SIZE; c++)
            if (inventory_fits(inv, c, r, sx, sy, NULL)) {
                if (col) *col = c;
                if (row) *row = r;
                return 0;
            }
    return -1; /* свободного места под габариты нет */
}

/* ------------------------------------------------- предметы: изменение */

/* Занимает предметом it (якорь в col,row, размер sx x sy) его ячейки.
 * Без проверок — проверки делает inventory_fits у вызывающих. */
static void link_cells(inventory *inv, inventory_item *it,
                       int col, int row, int sx, int sy)
{
    for (int r = row; r < row + sy; r++)
        for (int c = col; c < col + sx; c++)
            inv->items_places[r][c] = it;
}

/* Освобождает все ячейки сетки, занятые предметом it. */
static void unlink_cells(inventory *inv, const inventory_item *it)
{
    for (int r = 0; r < INVENTORY_SIZE; r++)
        for (int c = 0; c < INVENTORY_SIZE; c++)
            if (inv->items_places[r][c] == it)
                inv->items_places[r][c] = NULL;
}

int inventory_add_item(inventory *inv, const game_object_desc *proto,
                       inventory_item **out)
{
    if (!inv || !proto)
        return -1;

    /* свободный слот в items_arr (записи не перемещаются — слоты
     * переиспользуются после удаления предметов) */
    inventory_item *slot = NULL;
    for (int i = 0; i < INVENTORY_ITEMS_MAX; i++)
        if (!inv->items_arr[i].proto) {
            slot = &inv->items_arr[i];
            break;
        }
    if (!slot)
        return -1; /* предметов уже максимум */

    int sx = proto->inventory_size_x;
    int sy = proto->inventory_size_y;
    int col = 0, row = 0;
    if (inventory_find_space(inv, sx, sy, &col, &row) != 0)
        return -1; /* места под габариты нет */

    memset(slot, 0, sizeof(*slot));
    slot->proto = proto;
    slot->count = proto->count > 0 ? proto->count : 1;
    slot->rotated = 0; /* вертикально, как в описании                  */
    slot->ammo_loaded = proto->magazine_size; /* магазин полный         */
    link_cells(inv, slot, col, row, sx, sy);

    if (out)
        *out = slot;
    return 0;
}

int inventory_move(inventory *inv, inventory_item *it,
                   int col, int row, int rotated)
{
    if (!inv || !it || !it->proto)
        return -1;

    /* прежнее место: снимаем предмет с сетки, но запоминаем его, чтобы
     * вернуть, если на новом месте габариты не помещаются */
    int old_col = -1, old_row = -1, old_rot = it->rotated;
    for (int r = 0; r < INVENTORY_SIZE && old_col < 0; r++)
        for (int c = 0; c < INVENTORY_SIZE; c++)
            if (inv->items_places[r][c] == it) {
                old_col = c;
                old_row = r;
                break;
            }

    unlink_cells(inv, it);

    int sx, sy;
    it->rotated = rotated;
    inventory_item_size(it, &sx, &sy);

    if (inventory_fits(inv, col, row, sx, sy, it)) {
        link_cells(inv, it, col, row, sx, sy);
        return 0;
    }

    /* не помещается — предмет возвращается на старую ячейку */
    it->rotated = old_rot;
    if (old_col >= 0) {
        inventory_item_size(it, &sx, &sy);
        link_cells(inv, it, old_col, old_row, sx, sy);
    }
    return -1;
}

void inventory_unlink(inventory *inv, inventory_item *it)
{
    if (!inv || !it)
        return;
    unlink_cells(inv, it);
}

void inventory_remove(inventory *inv, inventory_item *it)
{
    if (!inv || !it)
        return;
    unlink_cells(inv, it);
    if (inv->selected_weapon == it)
        inv->selected_weapon = NULL;
    memset(it, 0, sizeof(*it)); /* proto == NULL — слот свободен */
}
