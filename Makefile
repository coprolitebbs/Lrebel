# Lrebel — пошаговая стратегия в стиле Rebel Star (DOS). Этап 1.
#
# Сборка: make          (бинарный файл build/lrebel)
# Запуск: make run      (из корня репозитория: нужны config/ и img/)
# Очистка: make clean
#
# Единственная внешняя зависимость — SDL2 (dev-пакет):
#   Debian/Ubuntu: apt install libsdl2-dev
#   macOS:         brew install sdl2
# Заголовки ищутся через pkg-config, при его отсутствии — через sdl2-config.

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -MMD -MP
LDFLAGS ?=

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs)

SRC_DIR := src
OBJ_DIR := build
BIN     := $(OBJ_DIR)/lrebel

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(SDL_LIBS) -lm

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) Makefile | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -I$(SRC_DIR) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(OBJ_DIR)

.PHONY: all run clean

-include $(DEPS)
