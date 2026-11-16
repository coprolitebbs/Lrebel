/*
 * font.h — отрисовка текста внутриигровым шрифтом img/font.png.
 *
 * Шрифт состоит из квадратных тайлов 64x64, по 16 тайлов в строке.
 * Порядок тайлов задаётся строкой chartable (Windows-1251: один байт —
 * один символ). Номер (number) символа в chartable переводится в
 * координаты тайла:
 *     Y = (int)(number / 16);
 *     X = number - (Y * 16);
 * где Y — номер ряда тайлов, если считать по вертикали СНИЗУ
 * изображения img/font.png, а X — колонка тайла.
 */
#ifndef FONT_H
#define FONT_H

/*
 * Инициализация шрифта.
 *   chartable_win1251 — последовательность символов chartable, уже
 *                       перекодированная в Windows-1251 (1 символ = 1 байт);
 *   font_image_path   — путь к изображению шрифта (img/font.png).
 * Возвращает 0 при успехе, -1 при ошибке.
 */
int  font_init(const char *chartable_win1251, const char *font_image_path);

/* Освобождает изображение шрифта. */
void font_shutdown(void);

/*
 * Отрисовка строки.
 *   str_to_print — строка в кодировке Windows-1251;
 *   x, y         — координаты левого верхнего угла строки на экране;
 *   size         — относительный размер символа: 1.0f — высота экрана.
 * Каждый символ занимает квадратную ячейку размером size * высота_экрана.
 */
void draw_string(const char *str_to_print, int x, int y, float size);

/* Ширина строки в пикселях (для центрирования и т.п.). */
int  string_width(const char *str_to_print, float size);

#endif /* FONT_H */
