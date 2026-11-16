/*
 * utf8_win1251.c — перекодировка UTF-8 -> Windows-1251.
 *
 * Алгоритм соответствует исходной реализации convertUtf8ToWin1251 (C++),
 * перенесённой на Си с массивом WIN1251_TABLE[256]:
 *   - ASCII (0x00..0x7F) пропускается через таблицу без изменений;
 *   - 2-байтовая последовательность UTF-8 даёт код 0x0080..0x07FF,
 *     3-байтовая — 0x0800..0xFFFF;
 *   - коды русского алфавита 0x410..0x44F отображаются в 0xC0..0xFF
 *     через WIN1251_TABLE[(code - 0x410) + 0xC0];
 *   - символы вне таблицы заменяются на '?'.
 * Отличия от исходника (исправления): добавлены проверки выхода за
 * границы строки и поддержка букв 'Ё' (U+0401 -> 0xA8) и 'ё' (U+0451 -> 0xB8),
 * без которых буква Ё из chartable/config терялась бы и превращалась в '?'.
 */
#include <string.h>

#include "utf8_win1251.h"

char WIN1251_TABLE[256];

static int g_table_ready = 0;

void win1251_init_table(void)
{
    for (int i = 0; i < 256; i++)
        WIN1251_TABLE[i] = '?';

    /* ASCII — тождественное отображение */
    for (int i = 0; i < 0x80; i++)
        WIN1251_TABLE[i] = (char)i;

    WIN1251_TABLE[0xA8] = (char)0xA8; /* Ё */
    WIN1251_TABLE[0xB8] = (char)0xB8; /* ё */

    /* русские буквы А..я: 0x410..0x44F -> 0xC0..0xFF (порядок Windows-1251) */
    for (int i = 0; i < 64; i++)
        WIN1251_TABLE[0xC0 + i] = (char)(0xC0 + i);

    g_table_ready = 1;
}

/* Код Unicode -> байт Windows-1251. */
static char unicode_to_win1251(unsigned short code)
{
    if (code == 0x401)
        return (char)0xA8; /* Ё */
    if (code == 0x451)
        return (char)0xB8; /* ё */
    if (code >= 0x410 && code <= 0x44F)
        return WIN1251_TABLE[(code - 0x410) + 0xC0];
    return '?';
}

int utf8_to_win1251(const char *utf8_string, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return -1;
    if (!utf8_string)
        utf8_string = "";
    if (!g_table_ready)
        win1251_init_table();

    const unsigned char *bytes = (const unsigned char *)utf8_string;
    size_t len = strlen(utf8_string);
    size_t out_pos = 0;

    for (size_t i = 0; i < len;) {
        unsigned char ch = bytes[i++];

        if (out_pos + 1 >= out_size)
            goto overflow;

        if ((ch & 0x80) == 0) {
            out[out_pos++] = WIN1251_TABLE[ch];
        } else if ((ch & 0xE0) == 0xC0) {
            if (i >= len)
                goto question;
            unsigned short utf8_code =
                (unsigned short)(((ch & 0x1F) << 6) | (bytes[i++] & 0x3F));
            out[out_pos++] = unicode_to_win1251(utf8_code);
        } else if ((ch & 0xF0) == 0xE0) {
            if (i + 1 >= len)
                goto question;
            unsigned short utf8_code = (unsigned short)(((ch & 0x0F) << 12) |
                                                        ((bytes[i] & 0x3F) << 6) |
                                                        (bytes[i + 1] & 0x3F));
            i += 2;
            out[out_pos++] = unicode_to_win1251(utf8_code);
        } else {
question:
            out[out_pos++] = '?';
        }
        continue;

overflow:
        out[out_pos] = '\0';
        return -1;
    }

    out[out_pos] = '\0';
    return (int)out_pos;
}
