/*
 * inventory.h — инвентарь персонажа: предметы и сетка ячеек.
 *
 * Инвентарь хранит предметы персонажа и пригоден и для редактора
 * (настройка снаряжения), и для основной игры. Устройство:
 *
 *   items_arr     — массив ВСЕХ предметов инвентаря, максимум
 *                   INVENTORY_ITEMS_MAX записей. Предмет в инвентаре —
 *                   структура inventory_item: ссылка на свой прототип
 *                   (описание game_object_desc из config/objects.cfg) и
 *                   ситуативные параметры — количество в инвентаре, угол
 *                   поворота (вертикально или горизонтально) и текущее
 *                   количество патронов в магазине (для оружия).
 *                   Запись с proto == NULL свободна; записи никогда не
 *                   перемещаются, поэтому указатели на предметы остаются
 *                   действительными всё время жизни инвентаря.
 *   items_places  — сетка инвентаря INVENTORY_SIZE x INVENTORY_SIZE
 *                   (15x15) указателей: NULL — ячейка пуста, иначе —
 *                   ссылка на предмет из items_arr, занимающий ячейку
 *                   (предмет размером WxH занимает W x H ячеек своей
 *                   лево-верхней ячейкой-якорем).
 *   selected_weapon — ссылка на предмет из items_arr, выбранный в
 *                   качестве основного оружия. Размер магазина берется
 *                   из прототипа (game_object_desc.magazine_size),
 *                   текущее количество патронов — в inventory_item
 *                   самого предмета (ammo_loaded).
 *
 * Линейные размеры предмета в клетках задаются в прототипе
 * (game_object_desc.inventory_size_x / inventory_size_y); угол поворота
 * предмета (rotated) переставляет их местами: 0 — вертикально (как в
 * описании), 1 — горизонтально.
 *
 * Модуль не зависит ни от графики, ни от платформы — только от описаний
 * объектов (objects.h).
 */
#ifndef INVENTORY_H
#define INVENTORY_H

#include "objects.h"

/* Сторона квадратной сетки инвентаря (ячеек). */
#define INVENTORY_SIZE       15
/* Максимум предметов в инвентаре. */
#define INVENTORY_ITEMS_MAX  225

/* Предмет в инвентаре: ссылка на прототип + ситуативные параметры. */
typedef struct inventory_item {
    const game_object_desc *proto; /* прототип из config/objects.cfg     */
    int count;        /* количество в инвентаре (стопки боеприпасов)     */
    int rotated;      /* угол поворота: 0 — вертикально, 1 — горизонтально */
    int ammo_loaded;  /* патронов в магазине сейчас (только оружие)      */
} inventory_item;

/* Инвентарь персонажа. */
typedef struct inventory {
    inventory_item  items_arr[INVENTORY_ITEMS_MAX]; /* все предметы      */
    inventory_item *items_places[INVENTORY_SIZE][INVENTORY_SIZE]; /* сетка */
    inventory_item *selected_weapon; /* основное оружие (или NULL)       */
} inventory;

/* Готовит пустой инвентарь. */
void inventory_init(inventory *inv);

/* Число занятых записей items_arr (не свободных слотов). */
int  inventory_used_count(const inventory *inv);

/* Размеры предмета в клетках с учетом угла поворота. */
void inventory_item_size(const inventory_item *it, int *sx, int *sy);

/* Предмет, занимающий ячейку (col, row), или NULL (вне сетки — NULL). */
inventory_item *inventory_item_at(const inventory *inv, int col, int row);

/* Помещается ли предмет размером sx x sy в ячейку (col, row) сетки:
 * все его ячейки должны быть в пределах сетки и свободны (клетки,
 * занятые самим предметом except, считаются свободными). */
int  inventory_fits(const inventory *inv, int col, int row,
                    int sx, int sy, const inventory_item *except);

/* Первое свободное место размером sx x sy (сканирование сетки сверху
 * вниз, слева направо); 0 при успехе (координаты в *col, *row),
 * -1 — места нет. */
int  inventory_find_space(const inventory *inv, int sx, int sy,
                          int *col, int *row);

/* Добавление предмета по прототипу: отдельная функция сканирования
 * места и добавления — пригодится и в основной игре (подбор предметов).
 * Предмет размещается на первом свободном месте (вертикально), магазин
 * оружия заполняется полностью (ammo_loaded = magazine_size прототипа),
 * количество берется из прототипа (count). 0 при успехе, -1 — в
 * инвентаре нет места под габариты предмета; созданный предмет
 * возвращается в *out (если out != NULL). */
int  inventory_add_item(inventory *inv, const game_object_desc *proto,
                        inventory_item **out);

/* Перестановка предмета в ячейку (col, row) с заданным углом поворота.
 * Предмет снимается со старого места и размещается заново. Если
 * габариты не помещаются — предмет возвращается на прежнее место
 * (с прежним углом поворота) и возвращается -1. 0 при успехе. */
int  inventory_move(inventory *inv, inventory_item *it,
                    int col, int row, int rotated);

/* Снимает предмет с сетки, не удаляя сам предмет (начало перетаскивания
 * мышью); ссылка selected_weapon сохраняется. */
void inventory_unlink(inventory *inv, inventory_item *it);

/* Удаляет предмет из инвентаря совсем (слот items_arr освобождается,
 * ссылка основного оружия сбрасывается, если указывала на предмет). */
void inventory_remove(inventory *inv, inventory_item *it);

/* Вес содержимого инвентаря, кг: сумма весов предметов из прототипов
 * (weight_kg), умноженных на количество в стопке; используется для
 * расчета числа шагов за игровой ход (objects_char_steps). */
float inventory_weight_kg(const inventory *inv);

#endif /* INVENTORY_H */
