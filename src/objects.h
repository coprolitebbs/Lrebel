/*
 * objects.h — описания игровых объектов и персонажей (config/objects.cfg).
 *
 * Подбираемый/бросаемый объект — оружие, боеприпас, аптечка или поверап.
 * Описание каждого объекта лежит в config/objects.cfg в секции {objectN}
 * (N — с 1, без пропусков) и соответствует N-му тайлу строки 4
 * img/gtiles.png: тип объекта на карте — OBJ_TYPE_WEAPON_FIRST + N - 1
 * (см. map.h). Файл загружается при старте игры (см. main.c) и живёт
 * всё время работы приложения.
 *
 * Формат секции {objectN} (кодировка файла — UTF-8, строки внутри
 * программы хранятся в Windows-1251 после перекодировки):
 *   name=Название
 *   desc1..desc3=Краткое описание, три строки
 *   type=weapon | ammo | medkit | powerup
 *   max_count=Максимальное количество (по умолчанию: оружие/аптечка/
 *              поверап — 1, боеприпас — 20)
 *   count=Реальное количество (по умолчанию равно max_count)
 *   damage=Повреждение за 1 выстрел (только оружие)
 *   reload_turns=Время перезарядки в ходах (только оружие)
 *   magazine_size=Размер магазина (только оружие)
 *   weight=Вес одной единицы, кг
 *   inv_size_x / inv_size_y — линейные размеры предмета в инвентаре в
 *              клетках (по умолчанию: оружие — 1x2, боеприпасы, аптечка
 *              и поверапы — 1x1);
 *   inv_img_row / inv_img_col — ряд и колонка картинки предмета в
 *              инвентаре в img/hudimages.png, в клетках сетки 32x32
 *              (картинка занимает inv_size_x x inv_size_y клеток; по
 *              умолчанию — ряд 13, колонка N); при загрузке из них
 *              вычисляется пиксельный прямоугольник inv_img_*;
 *   hud_img_row / hud_img_col — ряд и колонка большого тайла предмета
 *              128x128 в img/hudimages.png (1 — верхний левый; тайлы
 *              нумеруются по строкам сверху вниз). Если ключи не заданы,
 *              большого тайла у предмета нет.
 *
 * Типы персонажей (фракции) описываются секциями {char_type1}..{char_type3}:
 *   name=Название
 *   control=player | cpu
 *   health=Здоровье персонажа (по умолчанию 100)
 *   hud_img_row_full / hud_img_col_full — координаты тайла персонажа
 *              128x128 в img/hudimages.png при полном здоровье;
 *   hud_img_row_damaged / hud_img_col_damaged — тайл поврежденного;
 *   hud_img_row_dead / hud_img_col_dead — тайл мертвого (трупа).
 *              Без ключей используется тайл 3*faction+состояние.
 * Конкретные персонажи — секциями {characterN}:
 *   name=Имя
 *   age=Возраст
 *   sex=male | female
 *   faction=Номер фракции 1..3 (секция {char_typeN})
 *   control=player | cpu (по умолчанию — как у фракции)
 *   steps=Дефолтное количество шагов за 1 ход
 *   vis_range=Дальность видимости — радиус круга (в элементах карты),
 *              который раскрывается вокруг персонажа ("туман войны");
 *              по умолчанию 12 (CHAR_DEFAULT_VIS_RANGE)
 */
#ifndef OBJECTS_H
#define OBJECTS_H

#define OBJECT_NAME_MAX   64 /* байт на имя (Windows-1251, 1 байт/символ) */
#define OBJECT_DESC_LINES 3  /* строк краткого описания                   */
#define OBJECT_DESC_MAX   96 /* байт на строку описания                   */
#define OBJECTS_MAX       32 /* максимум описаний объектов в конфиге      */
#define CHARACTERS_MAX    64 /* максимум персонажей в конфиге             */

/* Дальность видимости персонажа по умолчанию (радиус раскрытия
 * "тумана войны" вокруг юнита, элементов карты). */
#define CHAR_DEFAULT_VIS_RANGE 12

/* Ряд img/hudimages.png (в клетках сетки 32x32), в котором лежат
 * картинки предметов для инвентаря (ряд 13 = y 384 пикселя). */
#define OBJECT_INV_IMG_ROW 13

/* Размер большого тайла предмета/персонажа в img/hudimages.png. */
#define OBJECT_HUD_IMG_PX 128

/* Класс подбираемого объекта. */
typedef enum {
    OBJ_CLASS_WEAPON = 0, /* оружие             */
    OBJ_CLASS_AMMO,       /* боеприпас          */
    OBJ_CLASS_MEDKIT,     /* аптечка            */
    OBJ_CLASS_POWERUP,    /* поверап иного типа */
    OBJ_CLASS_COUNT
} object_class;

/* Линейные размеры предмета в инвентаре по умолчанию (в клетках сетки
 * 15x15): оружие — 1x2, боеприпасы, аптечка и поверапы — 1x1; в
 * config/objects.cfg переопределяется ключами inv_size_x/inv_size_y. */
#define OBJECT_INV_SIZE_LIGHT_X 1 /* легкое оружие (пистолеты, ПП)      */
#define OBJECT_INV_SIZE_LIGHT_Y 2
#define OBJECT_INV_SIZE_HEAVY_X 1 /* тяжелое оружие (винтовки, пулеметы)*/
#define OBJECT_INV_SIZE_HEAVY_Y 3
#define OBJECT_INV_SIZE_SMALL_X 1 /* боеприпас, аптечка, поверап        */
#define OBJECT_INV_SIZE_SMALL_Y 1

/* Описание подбираемого/бросаемого объекта (config/objects.cfg). */
typedef struct game_object_desc {
    char         name[OBJECT_NAME_MAX];     /* название                    */
    char         desc[OBJECT_DESC_LINES][OBJECT_DESC_MAX]; /* описание     */
    object_class cls;                       /* класс объекта               */
    int          max_count;  /* максимальное количество (стопка)            */
    int          count;      /* реальное количество (по умолчанию = max)    */
    int          damage;     /* повреждение за 1 выстрел (оружие)           */
    int          reload_turns; /* время перезарядки, ходов (оружие)         */
    int          magazine_size; /* размер магазина (оружие)                 */
    float        weight_kg;  /* вес одной единицы, кг                       */
    /* --- инвентарь --- */
    int inventory_size_x; /* горизонтальный размер предмета в клетках        */
    int inventory_size_y; /* вертикальный размер предмета в клетках          */
    /* Картинка предмета в инвентаре — прямоугольник в img/hudimages.png
     * (клетки сетки 32x32, размер inv_size_x x inv_size_y клеток;
     * конфиг — ключи inv_img_row/inv_img_col), пиксели.
     * Большой тайл предмета 128x128 в том же атласе — прямоугольник
     * hud_img_* (конфиг — ключи hud_img_row/hud_img_col); hud_img_w == 0
     * означает, что большого тайла у предмета нет. */
    int inv_img_x, inv_img_y, inv_img_w, inv_img_h;
    int hud_img_x, hud_img_y, hud_img_w, hud_img_h;
} game_object_desc;

/* Фракция (тип персонажа); названия и флаг player — в config/objects.cfg. */
typedef enum {
    CHAR_FACTION_SPETSNAZ = 0, /* Подразделение "Спецназ-9" — игрок  */
    CHAR_FACTION_REBELS,       /* Повстанцы и террористы — CPU       */
    CHAR_FACTION_CYBORGS,      /* Киборги и роботы — CPU             */
    CHAR_FACTION_COUNT
} char_faction;

/* Пол персонажа. */
enum { CHAR_SEX_MALE = 0, CHAR_SEX_FEMALE, CHAR_SEX_COUNT };

/* Состояние персонажа для выбора тайла 128x128 (img/hudimages.png):
 * полное здоровье, частичное, нулевое (мертв). */
typedef enum {
    CHAR_TILE_FULL = 0,
    CHAR_TILE_DAMAGED,
    CHAR_TILE_DEAD,
    CHAR_TILE_STATE_COUNT
} char_tile_state;

/* Описание персонажа (секция {characterN} в config/objects.cfg). */
typedef struct character_desc {
    char         name[OBJECT_NAME_MAX]; /* имя                          */
    int          age;                   /* возраст                      */
    int          sex;                   /* CHAR_SEX_*                   */
    char_faction faction;               /* фракция (тип персонажа)      */
    int          is_player;             /* 1 — управляется игроком      */
    int          steps_per_turn;        /* дефолтных шагов за 1 ход     */
    int          vis_range;             /* дальность видимости: радиус круга
                                         * (элементов карты), который раскрывается
                                         * вокруг персонажа ("туман войны")     */
} character_desc;

/* Загружает описания из config/objects.cfg: объекты ({objectN}), фракции
 * ({char_typeN}) и персонажей ({characterN}). Файл должен быть в UTF-8.
 * При отсутствии файла описания не загружаются (остаются встроенные
 * названия фракций), возвращается -1; 0 при успехе. */
int objects_load(const char *cfg_path);

/* Число загруженных описаний объектов и доступ к описанию (index с 0). */
int objects_desc_count(void);
const game_object_desc *objects_desc(int index);

/* Число загруженных персонажей и доступ к описанию (index с 0). */
int objects_character_count(void);
const character_desc *objects_character(int index);

/* Название фракции (Windows-1251) и флаг управления игроком. */
const char *objects_faction_name(char_faction faction);
int         objects_faction_is_player(char_faction faction);

/* Здоровье персонажа фракции (максимальное и текущее при появлении на
 * карте; config/objects.cfg, ключ health секции {char_typeN}). */
int objects_faction_health(char_faction faction);

/* Координаты тайла персонажа фракции 128x128 в img/hudimages.png для
 * состояния st (полное/частичное/нулевое здоровье) — в пикселях атласа
 * (левый верхний угол). 0 при успехе, -1 если координаты не заданы. */
int objects_faction_hud_img(char_faction faction, char_tile_state st,
                            int *x, int *y);

#endif /* OBJECTS_H */
