#ifndef PACMAN_COMMON_H
#define PACMAN_COMMON_H

#include <stddef.h>

#define PACMAN_GHOST_COUNT 4
#define PACMAN_MAX_MAP_ROWS 64
#define PACMAN_MAX_MAP_COLS 64
#define PACMAN_INITIAL_LIVES 3
#define DEFAULT_PACMAN_PRIORITY 20
#define DEFAULT_ENEMY_PRIORITY 30
#define MIN_PRIORITY 0
#define MAX_PRIORITY 100

typedef struct {
    int row;
    int col;
} Position;

typedef enum {
    PACMAN_OK = 0,
    PACMAN_ERROR = 1
} PacmanStatus;

#endif
