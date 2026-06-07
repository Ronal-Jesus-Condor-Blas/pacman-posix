#ifndef PACMAN_MOVEMENT_H
#define PACMAN_MOVEMENT_H

#include <stddef.h>

typedef struct {
    size_t count;
} MovementFile;

int movement_load_file(const char *case_dir, const char *file_name, MovementFile *moves);

#endif
