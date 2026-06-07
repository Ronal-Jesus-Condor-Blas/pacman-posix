#ifndef PACMAN_COMMON_H
#define PACMAN_COMMON_H

#include <stddef.h>

#define PACMAN_GHOST_COUNT 4

typedef struct {
    int row;
    int col;
} Position;

typedef enum {
    PACMAN_OK = 0,
    PACMAN_ERROR = 1
} PacmanStatus;

#endif
