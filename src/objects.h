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
 *
 * Типы персонажей (фракции) описываются секциями {char_type1}..{char_type3}
 * (name, control=player|cpu), конкретные персонажи — секциями {characterN}:
 *   name=Имя
 *   age=Возраст
 *   sex=male | female
 *   faction=Номер фракции 1..3 (секция {char_typeN})
 *   control=player | cpu (по умолчанию — как у фракции)
 *   steps=Дефолтное количество шагов за 1 ход
 */
#ifndef OBJECTS_H
#define OBJECTS_H

#define OBJECT_NAME_MAX   64 /* байт на имя (Windows-1251, 1 байт/символ) */
#define OBJECT_DESC_LINES 3  /* строк краткого описания                   */
#define OBJECT_DESC_MAX   96 /* байт на строку описания                   */
#define OBJECTS_MAX       32 /* максимум описаний объектов в конфиге      */
#define CHARACTERS_MAX    64 /* максимум персонажей в конфиге             */

/* Класс подбираемого объекта. */
typedef enum {
    OBJ_CLASS_WEAPON = 0, /* оружие             */
    OBJ_CLASS_AMMO,       /* боеприпас          */
    OBJ_CLASS_MEDKIT,     /* аптечка            */
    OBJ_CLASS_POWERUP,    /* поверап иного типа */
    OBJ_CLASS_COUNT
} object_class;

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

/* Описание персонажа (секция {characterN} в config/objects.cfg). */
typedef struct character_desc {
    char         name[OBJECT_NAME_MAX]; /* имя                          */
    int          age;                   /* возраст                      */
    int          sex;                   /* CHAR_SEX_*                   */
    char_faction faction;               /* фракция (тип персонажа)      */
    int          is_player;             /* 1 — управляется игроком      */
    int          steps_per_turn;        /* дефолтных шагов за 1 ход     */
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

#endif /* OBJECTS_H */
