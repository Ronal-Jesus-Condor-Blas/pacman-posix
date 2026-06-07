#ifndef PACMAN_SHARED_H
#define PACMAN_SHARED_H

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

#include "common.h"
#include "map.h"

#define PACMAN_SHARED_NAME "/pacman_posix_shared_state"

typedef struct {
    int global_tick;
    int max_ticks;
    int game_over;

    int pacman_x;
    int pacman_y;
    int pacman_score;
    int pacman_lives;

    int ghost_x[PACMAN_GHOST_COUNT];
    int ghost_y[PACMAN_GHOST_COUNT];

    int collision_detected;
    int collision_tick;
    int collision_ghost_id;

    int prioridad_pacman;
    int prioridad_enemy;
    int pending_priority_pacman;
    int priority_request_active;
    int pending_priority_enemy;
    int enemy_priority_request_active;

    size_t map_rows;
    size_t map_cols;
    char map_grid[PACMAN_MAX_MAP_ROWS][PACMAN_MAX_MAP_COLS];

    sem_t sem_pacman_turn;
    sem_t sem_enemy_turn;
    sem_t sem_p1_done;
    sem_t sem_p2_done;

    pthread_mutex_t state_mutex;
    pthread_mutex_t priority_mutex;
    pthread_mutex_t collision_mutex;
} shared_state_t;

typedef struct {
    int fd;
    int sync_initialized;
    shared_state_t *state;
    const char *name;
} SharedMemory;

int shared_memory_create(SharedMemory *shared);
int shared_state_initialize(shared_state_t *state, const Map *map, int max_ticks);
void shared_state_print_summary(const shared_state_t *state);
int shared_memory_release(SharedMemory *shared);

#endif
