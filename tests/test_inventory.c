/*
 * test_inventory.c — юнит-тест логики инвентаря (inventory.h/.c) без графики.
 * Сборка: cc -std=c99 -Wall -Wextra -Isrc tests/test_inventory.c src/inventory.c \
 *             -o build/test_inventory
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "inventory.h"

static game_object_desc mkproto(object_class cls, int sx, int sy,
                                const char *name)
{
    game_object_desc d;
    memset(&d, 0, sizeof(d));
    snprintf(d.name, sizeof(d.name), "%s", name);
    d.cls = cls;
    d.inventory_size_x = sx;
    d.inventory_size_y = sy;
    d.count = 1;
    d.max_count = 1;
    d.magazine_size = (cls == OBJ_CLASS_WEAPON) ? 12 : 0;
    return d;
}

int main(void)
{
    game_object_desc pistol = mkproto(OBJ_CLASS_WEAPON, 1, 2, "pistol");
    game_object_desc rifle  = mkproto(OBJ_CLASS_WEAPON, 1, 3, "rifle");
    game_object_desc ammo   = mkproto(OBJ_CLASS_AMMO, 1, 1, "ammo");
    ammo.count = 20;
    ammo.max_count = 20;

    inventory inv;

    /* --- init и добавление --- */
    inventory_init(&inv);
    assert(inv.items_places[0][0] == NULL);
    assert(inv.selected_weapon == NULL);
    assert(inventory_used_count(&inv) == 0);

    inventory_item *p1 = NULL;
    assert(inventory_add_item(&inv, &pistol, &p1) == 0 && p1);
    assert(inventory_used_count(&inv) == 1);
    assert(p1->proto == &pistol);
    assert(p1->count == 1);
    assert(p1->rotated == 0);
    assert(p1->ammo_loaded == 12); /* магазин полон */
    int sx, sy;
    inventory_item_size(p1, &sx, &sy);
    assert(sx == 1 && sy == 2);
    /* занял ячейки (0,0) и (0,1) */
    assert(inventory_item_at(&inv, 0, 0) == p1);
    assert(inventory_item_at(&inv, 0, 1) == p1);
    assert(inventory_item_at(&inv, 1, 0) == NULL);

    inventory_item *a1 = NULL;
    assert(inventory_add_item(&inv, &ammo, &a1) == 0 && a1);
    assert(a1->count == 20); /* количество из прототипа */
    /* боеприпас 1x1 попал в первое свободное место — рядом с пистолетом */
    assert(inventory_item_at(&inv, 1, 0) == a1);

    /* --- сканирование места --- */
    int c, r;
    /* первый свободный кусок 1x3: сетка сканируется сверху вниз, слева
     * направо — рядом с пистолетом и боеприпасом (2,0), а не под ними */
    assert(inventory_find_space(&inv, 1, 3, &c, &r) == 0);
    assert(c == 2 && r == 0);
    assert(inventory_find_space(&inv, 3, 1, &c, &r) == 0);
    assert(c == 2 && r == 0);
    assert(inventory_fits(&inv, 0, 1, 1, 1, NULL) == 0); /* занято пистолетом */
    assert(inventory_fits(&inv, 14, 14, 1, 2, NULL) == 0); /* за границей */
    assert(inventory_fits(&inv, 5, 5, 1, 2, NULL) == 1);

    /* --- перестановка --- */
    assert(inventory_move(&inv, p1, 5, 5, 0) == 0);
    assert(inventory_item_at(&inv, 0, 0) == NULL);
    assert(inventory_item_at(&inv, 5, 5) == p1);
    assert(inventory_item_at(&inv, 5, 6) == p1);

    /* не помещается (у нижней границы) — возвращается на место */
    assert(inventory_move(&inv, p1, 14, 14, 0) == -1);
    assert(inventory_item_at(&inv, 5, 5) == p1);
    assert(inventory_item_at(&inv, 5, 6) == p1);
    assert(inventory_item_at(&inv, 14, 14) == NULL);

    /* перекрытие другим предметом — не помещается, остается на месте */
    assert(inventory_move(&inv, p1, 1, 0, 0) == -1);
    assert(inventory_item_at(&inv, 5, 5) == p1);

    /* поворот: 1x2 -> 2x1 */
    assert(inventory_move(&inv, p1, 8, 8, 1) == 0);
    assert(p1->rotated == 1);
    inventory_item_size(p1, &sx, &sy);
    assert(sx == 2 && sy == 1);
    assert(inventory_item_at(&inv, 9, 8) == p1);
    assert(inventory_item_at(&inv, 8, 9) == NULL);
    inventory_move(&inv, p1, 8, 8, 0); /* назад в вертикальное */

    /* --- unlink/remove --- */
    inventory_unlink(&inv, p1);
    assert(inventory_item_at(&inv, 8, 8) == NULL);
    assert(inventory_used_count(&inv) == 2); /* предмет еще жив */
    assert(inventory_move(&inv, p1, 8, 8, 0) == 0); /* вернуть на место */

    inv.selected_weapon = p1;
    inventory_remove(&inv, p1);
    assert(inventory_used_count(&inv) == 1);
    assert(inv.selected_weapon == NULL); /* ссылка на оружие сброшена */
    assert(inventory_item_at(&inv, 8, 8) == NULL);
    assert(inventory_item_at(&inv, 8, 9) == NULL);

    /* слот переиспользуется (записи не перемещаются) */
    inventory_item *p2 = NULL;
    assert(inventory_add_item(&inv, &rifle, &p2) == 0);
    assert(p2 == p1); /* первый освободившийся слот */

    /* --- заполнение до предела: 225 предметов 1x1 --- */
    inventory big;
    inventory_init(&big);
    int added = 0;
    for (int i = 0; i < INVENTORY_ITEMS_MAX; i++)
        if (inventory_add_item(&big, &ammo, NULL) == 0)
            added++;
    assert(added == INVENTORY_ITEMS_MAX); /* 225 -> ровно вся сетка 15x15 */
    assert(inventory_add_item(&big, &ammo, NULL) == -1); /* больше нет места */
    assert(inventory_add_item(&big, &rifle, NULL) == -1); /* и под 1x3 тоже */

    printf("test_inventory: все проверки пройдены\n");
    return 0;
}
