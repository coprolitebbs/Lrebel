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

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

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
static int g_mouse_left_pressed  = 0; /* фронт нажатия ЛКМ */
static int g_mouse_left_released = 0; /* фронт отпускания ЛКМ */
static int g_mouse_right_pressed  = 0;
static int g_mouse_right_released = 0;

/* удерживаемые клавиши: g_keys_held[keycode & KEYMAP_MASK];
 * состояние ведётся по событиям KEYDOWN/KEYUP из plat_poll_events,
 * поэтому работает одинаково и с реальным вводом, и с синтетикой */
#define KEYMAP_MASK 1023
static unsigned char g_keys_held[KEYMAP_MASK + 1];
static int g_key_ctrl = 0; /* Ctrl удерживается (по keysym.mod) */

/* ------------------------------------------------------------- init/shutdown */

int plat_init(int win_w, int win_h, const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "plat_init: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    /* пиксель-арт: масштабирование тайлов без сглаживания */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    memset(g_keys_held, 0, sizeof(g_keys_held));
    g_key_ctrl = 0;

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

/* Общая загрузка PNG: декодирование stb_image сразу в RGBA, затем
 * создание текстуры. Если key_r >= 0, пиксели цвета (key_r,key_g,key_b)
 * получают нулевую альфу (color key): при отрисовке они пропускаются,
 * и сквозь них просвечивает предыдущий слой кадра. */
static plat_image *load_image_rgba(const char *path,
                                   int key_r, int key_g, int key_b)
{
    if (!path)
        return NULL;

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

    /* color key: точное совпадение цвета -> альфа 0 */
    if (key_r >= 0) {
        unsigned char kr = (unsigned char)key_r;
        unsigned char kg = (unsigned char)key_g;
        unsigned char kb = (unsigned char)key_b;
        unsigned char *p = pixels;
        for (int i = 0; i < w * h; i++, p += 4)
            if (p[0] == kr && p[1] == kg && p[2] == kb)
                p[3] = 0;
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
    /* альфа-перемешивание явно: прозрачные (color key) пиксели рисуются
     * через SDL_BLENDMODE_BLEND на всех платформах одинаково */
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

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

plat_image *plat_load_image(const char *path)
{
    return load_image_rgba(path, -1, -1, -1);
}

plat_image *plat_load_image_colorkey(const char *path,
                                     unsigned char r, unsigned char g,
                                     unsigned char b)
{
    return load_image_rgba(path, r, g, b);
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

void plat_draw_image_rot(const plat_image *img,
                         int src_x, int src_y, int src_w, int src_h,
                         int dst_x, int dst_y, int dst_w, int dst_h,
                         int quarter_turns)
{
    if (!img || !img->tex)
        return;
    SDL_Rect src = { src_x, src_y, src_w, src_h };
    SDL_Rect dst = { dst_x, dst_y, dst_w, dst_h };
    if ((quarter_turns % 4) == 0) {
        SDL_RenderCopy(g_renderer, img->tex, &src, &dst);
        return;
    }
    /* Поворот на 90/180/270 градусов вокруг центра конечного
     * прямоугольника. SDL_RenderCopyEx сначала масштабирует фрагмент в
     * dst-прямоугольник и только потом вращает его, поэтому при нечетном
     * числе поворотов dst подменяется на пред-поворотный (ширина и
     * высота переставляются, центр совпадает с центром конечного) — так
     * пропорции фрагмента не искажаются. Точка center в
     * SDL_RenderCopyEx задается относительно левого верхнего угла
     * dst-прямоугольника. Угол в SDL — против часовой стрелки. */
    int turns = quarter_turns % 4;
    int rw = (turns % 2) ? dst_h : dst_w; /* пред-поворотные размеры     */
    int rh = (turns % 2) ? dst_w : dst_h;
    SDL_Rect rdst = { dst_x + (dst_w - rw) / 2,
                      dst_y + (dst_h - rh) / 2, rw, rh };
    SDL_Point center = { rw / 2, rh / 2 };
    double angle = -90.0 * (double)turns;
    SDL_RenderCopyEx(g_renderer, img->tex, &src, &rdst, angle,
                     &center, SDL_FLIP_NONE);
}

void plat_draw_rect_frame(int x, int y, int w, int h,
                          unsigned char r, unsigned char g, unsigned char b)
{
    if (w <= 0 || h <= 0)
        return;
    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(g_renderer, &rect);
    /* вторая рамка со сдвигом — линия в 2 пикселя, заметнее на фоне карты */
    SDL_Rect inner = { x + 1, y + 1, w - 2, h - 2 };
    if (w > 2 && h > 2)
        SDL_RenderDrawRect(g_renderer, &inner);
}

void plat_draw_blink_frame(int x, int y, int w, int h,
                           unsigned char r, unsigned char g, unsigned char b)
{
    /* период мигания 400 мс: половину такта рамка видна, половину — нет;
     * часы платформенные (SDL_GetTicks / plat_ticks) */
    enum { BLINK_PERIOD_MS = 400 };
    unsigned phase = plat_ticks() % (unsigned)BLINK_PERIOD_MS;
    if (phase < (unsigned)BLINK_PERIOD_MS / 2)
        plat_draw_rect_frame(x, y, w, h, r, g, b);
}

unsigned int plat_ticks(void)
{
    return SDL_GetTicks();
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
        case SDL_KEYUP: {
            /* состояние клавиш ведётся по двум кодам: по коду символа,
             * который даёт текущая раскладка (keysym.sym), и по коду той
             * же физической клавиши в US-раскладке (SDL_GetKeyFromScancode).
             * Верхний уровень проверяет held-состояние и «последнюю
             * нажатую клавишу» по US-кодам (WASD, '1'..'4', 'i' и т.п.),
             * поэтому управление не зависит от выбранной раскладки. */
            int sym = (int)ev.key.keysym.sym;
            int us  = (int)SDL_GetKeyFromScancode(ev.key.keysym.scancode);
            g_keys_held[sym & KEYMAP_MASK] = (ev.type == SDL_KEYDOWN);
            if (us > 0)
                g_keys_held[us & KEYMAP_MASK] = (ev.type == SDL_KEYDOWN);
            g_key_ctrl = (ev.key.keysym.mod & KMOD_CTRL) ? 1 : 0;
            /* автоповтор не считается новым нажатием */
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                /* клавиатура навигации приводится к обычным кодам,
                 * чтобы верхний уровень не зависел от SDL-констант */
                if (us == SDLK_KP_PLUS)  us = '+';
                if (us == SDLK_KP_MINUS) us = '-';
                if (us > 0)
                    g_last_key = us;
            }
            break;
        }
        case SDL_MOUSEMOTION:
            g_mouse_x = ev.motion.x;
            g_mouse_y = ev.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            g_mouse_x = ev.button.x;
            g_mouse_y = ev.button.y;
            if (ev.button.button == SDL_BUTTON_LEFT) {
                if (ev.type == SDL_MOUSEBUTTONDOWN)
                    g_mouse_left_pressed = 1;
                else
                    g_mouse_left_released = 1;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                if (ev.type == SDL_MOUSEBUTTONDOWN)
                    g_mouse_right_pressed = 1;
                else
                    g_mouse_right_released = 1;
            }
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

int plat_key_held(int keycode)
{
    if (keycode <= 0)
        return 0;
    return g_keys_held[keycode & KEYMAP_MASK];
}

int plat_key_ctrl(void)
{
    return g_key_ctrl;
}

void plat_mouse_pos(int *x, int *y)
{
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
}

int plat_mouse_left_down(void)
{
    if (!g_window)
        return 0;
    return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) != 0;
}

int plat_mouse_left_pressed(void)
{
    int pressed = g_mouse_left_pressed;
    g_mouse_left_pressed = 0;
    return pressed;
}

int plat_mouse_left_released(void)
{
    int released = g_mouse_left_released;
    g_mouse_left_released = 0;
    return released;
}

int plat_mouse_right_down(void)
{
    if (!g_window)
        return 0;
    return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK) != 0;
}

int plat_mouse_right_pressed(void)
{
    int pressed = g_mouse_right_pressed;
    g_mouse_right_pressed = 0;
    return pressed;
}

int plat_mouse_right_released(void)
{
    int released = g_mouse_right_released;
    g_mouse_right_released = 0;
    return released;
}

void plat_delay_ms(int ms)
{
    if (ms > 0)
        SDL_Delay((Uint32)ms);
}

int plat_mkdir(const char *path)
{
    if (!path || !*path)
        return -1;
    if (mkdir(path, 0777) == 0)
        return 0;
    return (errno == EEXIST) ? 0 : -1;
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
