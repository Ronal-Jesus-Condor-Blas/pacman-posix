#ifndef PACMAN_MAP_H
#define PACMAN_MAP_H

#include <stddef.h>

#include "common.h"

typedef struct {
    char **cells;
    size_t rows;
    size_t cols;
    Position pacman_start;
    Position ghost_starts[PACMAN_GHOST_COUNT];
} Map;

int map_load(const char *case_dir, Map *map);
void map_print(const Map *map);
void map_free(Map *map);

#endif
