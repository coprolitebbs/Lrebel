/*
 * portable_posix.c — нижний (платформенный) уровень, этап 1.
 *
 * Реализация абстрактного интерфейса portable_posix.h через SDL2
 * для Linux и macOS. Декодирование PNG выполняется встроенным
 * (vendored) заголовочным модулем stb_image.h — таким образом единственной
 * внешней зависимостью остаётся SDL2, а при порте на другую платформу
 * достаточно заменить этот файл (PNG-декодер от графической библиотеки
 * не зависит).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include "portable_posix.h"

/* ------------------------------------------------------------- состояние */

struct plat_image {
    SDL_Texture *tex;
    int w, h;
};

struct plat_file {
    FILE *fp;
};

static SDL_Window   *g_window   = NULL;
static SDL_Renderer *g_renderer = NULL;
static int g_quit_requested = 0;
static int g_last_key = 0;
static int g_mouse_x = 0, g_mouse_y = 0;

/* ------------------------------------------------------------- init/shutdown */

int plat_init(int win_w, int win_h, const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "plat_init: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    /* пиксель-арт: масштабирование тайлов без сглаживания */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    g_window = SDL_CreateWindow(title,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                win_w, win_h,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        fprintf(stderr, "plat_init: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    /* аппаратный рендер с вертикальной синхронизацией; при его отсутствии
     * (например, фиктивный видеодрайвер) — программный */
    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED |
                                    SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer)
        g_renderer = SDL_CreateRenderer(g_window, -1, 0);
    if (!g_renderer) {
        fprintf(stderr, "plat_init: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return -1;
    }

    return 0;
}

void plat_shutdown(void)
{
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();
}

/* ------------------------------------------------------------- экран */

int plat_screen_width(void)
{
    int w = 0, h = 0;
    if (g_window)
        SDL_GetWindowSize(g_window, &w, &h);
    return w;
}

int plat_screen_height(void)
{
    int w = 0, h = 0;
    if (g_window)
        SDL_GetWindowSize(g_window, &w, &h);
    return h;
}

/* ------------------------------------------------------------- изображения */

plat_image *plat_load_image(const char *path)
{
    if (!path) return NULL;

    /* файл целиком читается абстрактными функциями платформы */
    plat_file *f = plat_file_open_read(path);
    if (!f) {
        fprintf(stderr, "plat_load_image: не удалось открыть '%s'\n", path);
        return NULL;
    }
    long fsize = plat_file_size(f);
    if (fsize <= 0) {
        fprintf(stderr, "plat_load_image: '%s': пустой или нечитаемый файл\n", path);
        plat_file_close(f);
        return NULL;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)fsize);
    if (!data) {
        fprintf(stderr, "plat_load_image: не хватает памяти для '%s'\n", path);
        plat_file_close(f);
        return NULL;
    }
    size_t got = plat_file_read(f, data, (size_t)fsize);
    plat_file_close(f);

    int w = 0, h = 0, n = 0;
    /* 4 канала: всегда приводим к RGBA — прозрачность шрифта сохраняется */
    unsigned char *pixels = stbi_load_from_memory(data, (int)got, &w, &h, &n, 4);
    free(data);
    if (!pixels) {
        fprintf(stderr, "plat_load_image: '%s': %s\n", path, stbi_failure_reason());
        return NULL;
    }

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        fprintf(stderr, "plat_load_image: '%s': surface: %s\n", path, SDL_GetError());
        stbi_image_free(pixels);
        return NULL;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_FreeSurface(surf);
    stbi_image_free(pixels);
    if (!tex) {
        fprintf(stderr, "plat_load_image: '%s': texture: %s\n", path, SDL_GetError());
        return NULL;
    }

    plat_image *img = (plat_image *)calloc(1, sizeof(*img));
    if (!img) {
        SDL_DestroyTexture(tex);
        return NULL;
    }
    img->tex = tex;
    img->w = w;
    img->h = h;
    return img;
}

void plat_free_image(plat_image *img)
{
    if (!img) return;
    if (img->tex)
        SDL_DestroyTexture(img->tex);
    free(img);
}

int plat_image_width(const plat_image *img)
{
    return img ? img->w : 0;
}

int plat_image_height(const plat_image *img)
{
    return img ? img->h : 0;
}

/* ------------------------------------------------------------- кадр */

void plat_clear_screen(unsigned char r, unsigned char g, unsigned char b)
{
    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    SDL_RenderClear(g_renderer);
}

void plat_draw_image(const plat_image *img,
                     int src_x, int src_y, int src_w, int src_h,
                     int dst_x, int dst_y, int dst_w, int dst_h)
{
    if (!img || !img->tex)
        return;
    SDL_Rect src = { src_x, src_y, src_w, src_h };
    SDL_Rect dst = { dst_x, dst_y, dst_w, dst_h };
    SDL_RenderCopy(g_renderer, img->tex, &src, &dst);
}

void plat_flip_frame(void)
{
    SDL_RenderPresent(g_renderer);
}

/* ------------------------------------------------------------- ввод */

int plat_poll_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            g_quit_requested = 1;
            break;
        case SDL_KEYDOWN:
            g_last_key = (int)ev.key.keysym.sym;
            break;
        case SDL_MOUSEMOTION:
            g_mouse_x = ev.motion.x;
            g_mouse_y = ev.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            g_mouse_x = ev.button.x;
            g_mouse_y = ev.button.y;
            break;
        default:
            break;
        }
    }
    return g_quit_requested;
}

int plat_last_key(void)
{
    int key = g_last_key;
    g_last_key = 0;
    return key;
}

void plat_mouse_pos(int *x, int *y)
{
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
}

/* ------------------------------------------------------------- файлы */

plat_file *plat_file_open_read(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    plat_file *f = (plat_file *)malloc(sizeof(*f));
    if (!f) {
        fclose(fp);
        return NULL;
    }
    f->fp = fp;
    return f;
}

plat_file *plat_file_open_write(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return NULL;
    plat_file *f = (plat_file *)malloc(sizeof(*f));
    if (!f) {
        fclose(fp);
        return NULL;
    }
    f->fp = fp;
    return f;
}

long plat_file_size(plat_file *file)
{
    if (!file || !file->fp)
        return -1;
    long cur = ftell(file->fp);
    if (cur < 0)
        return -1;
    if (fseek(file->fp, 0, SEEK_END) != 0)
        return -1;
    long size = ftell(file->fp);
    if (fseek(file->fp, cur, SEEK_SET) != 0)
        return -1;
    return size;
}

size_t plat_file_read(plat_file *file, void *buf, size_t len)
{
    if (!file || !file->fp)
        return 0;
    return fread(buf, 1, len, file->fp);
}

size_t plat_file_write(plat_file *file, const void *buf, size_t len)
{
    if (!file || !file->fp)
        return 0;
    return fwrite(buf, 1, len, file->fp);
}

void plat_file_close(plat_file *file)
{
    if (!file)
        return;
    if (file->fp)
        fclose(file->fp);
    free(file);
}
