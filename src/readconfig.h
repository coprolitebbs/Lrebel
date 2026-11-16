/*
 * readconfig.h — чтение конфигурационных ini-подобных файлов (UTF-8).
 *
 * Формат файла:
 *   - секция объявляется строкой в фигурных скобках: {section}
 *   - параметр: key=value (разделитель — первый '=' в строке,
 *     остальные '=' входят в значение)
 *   - комментарии: '#' или ';' в начале строки (после возможных пробелов)
 *   - пробелы и табуляции по краям имён секций, ключей и значений
 *     отбрасываются; пробелы внутри значения сохраняются
 *   - кодировка файла — UTF-8; значения хранятся в ней же, перекодировка
 *     (например, в Windows-1251) выполняется вызывающим кодом.
 *
 * Модуль работает ТОЛЬКО через абстрактные функции файлового доступа
 * платформенного уровня (plat_file_*); конкретная реализация —
 * в portable_posix.c.
 */
#ifndef READCONFIG_H
#define READCONFIG_H

typedef struct config config;

/* Загружает файл конфигурации. NULL при ошибке (не открылся, нет памяти). */
config *config_load(const char *path);

/* Полностью освобождает конфигурацию. */
void    config_free(config *cfg);

/* Значение параметра section/key либо default_value, если не найдено.
 * Имена секций и ключей сравниваются с учётом регистра. */
const char *config_get(const config *cfg,
                       const char *section, const char *key,
                       const char *default_value);

int   config_get_int(const config *cfg,
                     const char *section, const char *key,
                     int default_value);

float config_get_float(const config *cfg,
                       const char *section, const char *key,
                       float default_value);

#endif /* READCONFIG_H */
