/*
 * ui.h — абстрактный пользовательский интерфейс: кнопки, окна, прогрессбар.
 *
 * Описание отдельной кнопки — структура ui_button; все кнопки игры
 * хранятся в общем массиве ui_buttons (пригодится на следующих этапах).
 * Кнопка хранит указатель на типовую функцию-обработчик (callback),
 * вызываемую по нажатию; если указатель NULL — нажатие ни к чему не
 * приводит. Отрисовывается и реагирует на мышь только «отрисовываемая»
 * кнопка (флаг enabled).
 *
 * Графика кнопок, окон и прогрессбара берётся 9-срезами из единого
 * изображения img/windows.png; координаты и размеры элементов задаются
 * в config/windows.cfg (без хардкода в коде).
 *
 * Модуль работает только через абстрактные функции нижнего уровня
 * (portable_posix.h) и шрифт (font.h).
 */
#ifndef UI_H
#define UI_H

#define UI_BUTTON_CAPTION_MAX 64  /* байты Windows-1251 с завершающим '\0' */
#define UI_BUTTON_GLYPH_MAX   16  /* байты глифа (вместо caption) */
#define UI_BUTTONS_MAX        32  /* размер массива всех кнопок игры */

typedef void (*ui_callback)(void);

typedef struct ui_button {
    int x, y, w, h;                      /* прямоугольник кнопки на экране */
    char caption[UI_BUTTON_CAPTION_MAX]; /* надпись (Windows-1251) */
    char glyph[UI_BUTTON_GLYPH_MAX];     /* глиф вместо надписи: рисуется,
                                            если caption пуст (крестик и т.п.) */
    ui_callback on_press;                /* функция, запускаемая по нажатию */
    int enabled;                         /* 1 — отрисовывается и реагирует */
    int pressed;                         /* визуальное состояние нажатия */
} ui_button;

/* Массив всех кнопок игры. */
extern ui_button ui_buttons[UI_BUTTONS_MAX];
extern int       ui_buttons_count;

/* --- скин (img/windows.png + config/windows.cfg) --- */

/* Загружает скин: читает конфиг windows_cfg_path (секции {skin},
 * {button}, {button_pressed}, {window}, {progressbar}) и изображение.
 * Возвращает 0 при успехе, -1 при ошибке. */
int  ui_init(const char *windows_cfg_path);

/* Выгружает изображение скина. */
void ui_shutdown(void);

/* --- кнопки --- */

/* Добавляет кнопку в общий массив; возвращает её индекс или -1,
 * если массив полон. caption_1251 — надпись в Windows-1251. */
int  ui_button_add(int x, int y, int w, int h,
                   const char *caption_1251, ui_callback on_press);

/* Задаёт глиф кнопки (Windows-1251), который рисуется вместо надписи,
 * когда caption пуст (например, крестик закрытия окна). */
void ui_button_set_glyph(int index, const char *glyph_1251);

/* Отрисовка отдельной кнопки по её состоянию (enabled/pressed). */
void ui_button_draw(const ui_button *btn);

/* Проверка нажатия на кнопку: возвращает 1, если левая кнопка мыши
 * нажата внутри прямоугольника отрисовываемой (enabled) кнопки. */
int  ui_button_check_press(const ui_button *btn);

/* Обработка всех кнопок: опрос мыши, визуальное нажатие, вызов
 * on_press при клике. Вызывать ОДИН раз за кадр до отрисовки. */
void ui_buttons_process(void);

/* Отрисовка всех отрисовываемых кнопок. */
void ui_buttons_draw(void);

/* --- окно и прогрессбар --- */

/* Типовое информационное окно (рамка + полупрозрачный фон-паттерн). */
void ui_window_draw(int x, int y, int w, int h);

/* Прогрессбар; fraction — заполнение 0.0..1.0. */
void ui_progressbar_draw(int x, int y, int w, int h, float fraction);

#endif /* UI_H */
