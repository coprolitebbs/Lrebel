/*
 * portable_posix.h — абстрактный интерфейс платформенного (нижнего) уровня.
 *
 * Верхний уровень игры (логика) вызывает ТОЛЬКО функции, объявленные здесь,
 * и не знает ни о конкретной ОС, ни о графической библиотеке. Этап 1:
 * реализация через SDL2 для Linux и macOS — см. portable_posix.c.
 * Портирование на другую платформу = реализация этих же функций в новом
 * модуле (например, portable_win.c); верхний уровень не меняется.
 */
#ifndef PORTABLE_POSIX_H
#define PORTABLE_POSIX_H

#include <stddef.h>

#define DEFAULT_WIN_W 1024
#define DEFAULT_WIN_H 768

/* ---------------------------------------------------------------- изображения */

typedef struct plat_image plat_image;

/* Загружает изображение (PNG) из файловой системы. NULL при ошибке. */
plat_image *plat_load_image(const char *path);
void        plat_free_image(plat_image *img);
int         plat_image_width(const plat_image *img);
int         plat_image_height(const plat_image *img);

/* ---------------------------------------------------------------- экран */

/* Текущее разрешение окна/экрана в пикселях. */
int plat_screen_width(void);
int plat_screen_height(void);

/* ---------------------------------------------------------------- кадр */

/* Очищает кадр сплошным цветом (RGB, 0..255). */
void plat_clear_screen(unsigned char r, unsigned char g, unsigned char b);

/* Рисует фрагмент изображения src_x,src_y,src_w,src_h, масштабируя его
 * в прямоугольник dst_x,dst_y,dst_w,dst_h кадра. */
void plat_draw_image(const plat_image *img,
                     int src_x, int src_y, int src_w, int src_h,
                     int dst_x, int dst_y, int dst_w, int dst_h);

/* Завершает кадр и показывает его на экране. */
void plat_flip_frame(void);

/* ---------------------------------------------------------------- ввод */

/* Разбирает очередь событий платформы. Возвращает 1, если поступил запрос
 * на закрытие окна (кнопка закрытия, Alt+F4, Cmd+Q и т.п.) — после этого
 * главный цикл должен завершиться; иначе 0. */
int plat_poll_events(void);

/* Код последней нажатой клавиши с предыдущего вызова (SDL_Keycode),
 * 0 — если клавиш не было. Вызов «поглощает» код. */
int plat_last_key(void);

/* Текущие координаты мыши в пикселях окна (или NULL). */
void plat_mouse_pos(int *x, int *y);

/* Левая кнопка мыши удерживается в данный момент: 1/0. */
int plat_mouse_left_down(void);

/* Левая кнопка мыши была НАЖАТА с предыдущего вызова: 1/0.
 * Вызов «поглощает» событие нажатия. */
int plat_mouse_left_pressed(void);

/* Левая кнопка мыши была ОТПУЩЕНА с предыдущего вызова: 1/0.
 * Вызов «поглощает» событие отпускания. */
int plat_mouse_left_released(void);

/* ---------------------------------------------------------------- файлы */

typedef struct plat_file plat_file;

plat_file *plat_file_open_read(const char *path);
plat_file *plat_file_open_write(const char *path);
/* Размер файла в байтах или -1 при ошибке. */
long        plat_file_size(plat_file *file);
/* Читает/пишет до len байт, возвращает фактически обработанное количество. */
size_t      plat_file_read(plat_file *file, void *buf, size_t len);
size_t      plat_file_write(plat_file *file, const void *buf, size_t len);
void        plat_file_close(plat_file *file);

/* ------------------------------------------------ инициализация/завершение */

/* Инициализирует платформу: создаёт окно win_w x win_h с заголовком title.
 * 0 при успехе, -1 при ошибке. */
int  plat_init(int win_w, int win_h, const char *title);
/* Полностью освобождает ресурсы платформы. */
void plat_shutdown(void);

/* Пауза в миллисекундах. */
void plat_delay_ms(int ms);

#endif /* PORTABLE_POSIX_H */
