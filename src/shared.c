#include "../include/shared.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int initialize_mutex(pthread_mutex_t *mutex, pthread_mutexattr_t *attr)
{
    int result = pthread_mutex_init(mutex, attr);

    if (result != 0) {
        errno = result;
        perror("pthread_mutex_init");
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int initialize_mutexes(shared_state_t *state)
{
    pthread_mutexattr_t attr;
    int attr_initialized = 0;
    int state_initialized = 0;
    int priority_initialized = 0;
    int result;

    result = pthread_mutexattr_init(&attr);
    if (result != 0) {
        errno = result;
        perror("pthread_mutexattr_init");
        return PACMAN_ERROR;
    }
    attr_initialized = 1;

    result = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (result != 0) {
        errno = result;
        perror("pthread_mutexattr_setpshared");
        goto fail;
    }

    if (initialize_mutex(&state->state_mutex, &attr) != PACMAN_OK) {
        goto fail;
    }
    state_initialized = 1;

    if (initialize_mutex(&state->priority_mutex, &attr) != PACMAN_OK) {
        goto fail;
    }
    priority_initialized = 1;

    if (initialize_mutex(&state->collision_mutex, &attr) != PACMAN_OK) {
        goto fail;
    }

    pthread_mutexattr_destroy(&attr);
    return PACMAN_OK;

fail:
    if (priority_initialized) {
        pthread_mutex_destroy(&state->priority_mutex);
    }
    if (state_initialized) {
        pthread_mutex_destroy(&state->state_mutex);
    }
    if (attr_initialized) {
        pthread_mutexattr_destroy(&attr);
    }
    return PACMAN_ERROR;
}

static int initialize_semaphore(sem_t *semaphore, unsigned int value)
{
    if (sem_init(semaphore, 1, value) != 0) {
        perror("sem_init");
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int initialize_semaphores(shared_state_t *state)
{
    int pacman_initialized = 0;
    int enemy_initialized = 0;
    int p1_initialized = 0;

    if (initialize_semaphore(&state->sem_pacman_turn, 0) != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    pacman_initialized = 1;

    if (initialize_semaphore(&state->sem_enemy_turn, 0) != PACMAN_OK) {
        goto fail;
    }
    enemy_initialized = 1;

    if (initialize_semaphore(&state->sem_p1_done, 0) != PACMAN_OK) {
        goto fail;
    }
    p1_initialized = 1;

    if (initialize_semaphore(&state->sem_p2_done, 0) != PACMAN_OK) {
        goto fail;
    }

    return PACMAN_OK;

fail:
    if (p1_initialized) {
        sem_destroy(&state->sem_p1_done);
    }
    if (enemy_initialized) {
        sem_destroy(&state->sem_enemy_turn);
    }
    if (pacman_initialized) {
        sem_destroy(&state->sem_pacman_turn);
    }
    return PACMAN_ERROR;
}

static void destroy_sync_objects(shared_state_t *state)
{
    sem_destroy(&state->sem_pacman_turn);
    sem_destroy(&state->sem_enemy_turn);
    sem_destroy(&state->sem_p1_done);
    sem_destroy(&state->sem_p2_done);

    pthread_mutex_destroy(&state->state_mutex);
    pthread_mutex_destroy(&state->priority_mutex);
    pthread_mutex_destroy(&state->collision_mutex);
}

static int copy_map_to_shared(shared_state_t *state, const Map *map)
{
    size_t row;
    size_t col;

    if (map->rows > PACMAN_MAX_MAP_ROWS || map->cols > PACMAN_MAX_MAP_COLS) {
        fprintf(stderr,
                "Error: mapa demasiado grande para memoria compartida (%zux%zu, maximo %dx%d).\n",
                map->rows,
                map->cols,
                PACMAN_MAX_MAP_ROWS,
                PACMAN_MAX_MAP_COLS);
        return PACMAN_ERROR;
    }

    state->map_rows = map->rows;
    state->map_cols = map->cols;

    for (row = 0; row < map->rows; ++row) {
        for (col = 0; col < map->cols; ++col) {
            state->map_grid[row][col] = map->cells[row][col];
        }
    }

    return PACMAN_OK;
}

static void copy_initial_positions(shared_state_t *state, const Map *map)
{
    size_t i;

    state->pacman_x = map->pacman_start.col;
    state->pacman_y = map->pacman_start.row;

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        state->ghost_x[i] = map->ghost_starts[i].col;
        state->ghost_y[i] = map->ghost_starts[i].row;
    }
}

static int parse_priority_env(const char *name, int default_value, int *priority)
{
    const char *value_text = getenv(name);
    char *end = NULL;
    long value;

    if (value_text == NULL || value_text[0] == '\0') {
        *priority = default_value;
        return PACMAN_OK;
    }

    errno = 0;
    value = strtol(value_text, &end, 10);
    if (errno != 0 || end == value_text || *end != '\0' ||
        value < MIN_PRIORITY || value > MAX_PRIORITY) {
        fprintf(stderr,
                "Error: %s debe estar entre %d y %d.\n",
                name,
                MIN_PRIORITY,
                MAX_PRIORITY);
        return PACMAN_ERROR;
    }

    *priority = (int)value;
    return PACMAN_OK;
}

static int load_initial_priorities(int *pacman_priority, int *enemy_priority)
{
    if (parse_priority_env("PACMAN_PRIORITY", DEFAULT_PACMAN_PRIORITY, pacman_priority) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (parse_priority_env("ENEMY_PRIORITY", DEFAULT_ENEMY_PRIORITY, enemy_priority) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("Prioridades iniciales usadas: pacman=%d enemy=%d\n", *pacman_priority, *enemy_priority);
    return PACMAN_OK;
}

int shared_memory_create(SharedMemory *shared)
{
    int fd;

    shared->fd = -1;
    shared->sync_initialized = 0;
    shared->state = NULL;
    shared->name = PACMAN_SHARED_NAME;

    if (shm_unlink(shared->name) != 0 && errno != ENOENT) {
        perror("shm_unlink");
        return PACMAN_ERROR;
    }

    fd = shm_open(shared->name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        perror("shm_open");
        return PACMAN_ERROR;
    }
    shared->fd = fd;

    if (ftruncate(shared->fd, (off_t)sizeof(shared_state_t)) != 0) {
        perror("ftruncate");
        shared_memory_release(shared);
        return PACMAN_ERROR;
    }

    shared->state = mmap(NULL,
                         sizeof(shared_state_t),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED,
                         shared->fd,
                         0);
    if (shared->state == MAP_FAILED) {
        perror("mmap");
        shared->state = NULL;
        shared_memory_release(shared);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

int shared_state_initialize(shared_state_t *state, const Map *map, int max_ticks)
{
    int pacman_priority;
    int enemy_priority;

    memset(state, 0, sizeof(*state));

    if (load_initial_priorities(&pacman_priority, &enemy_priority) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    state->global_tick = 0;
    state->max_ticks = max_ticks;
    state->game_over = 0;
    state->pacman_score = 0;
    state->pacman_lives = PACMAN_INITIAL_LIVES;
    state->collision_detected = 0;
    state->collision_tick = -1;
    state->collision_ghost_id = -1;
    state->prioridad_pacman = pacman_priority;
    state->prioridad_enemy = enemy_priority;
    state->pending_priority_pacman = pacman_priority;
    state->pending_priority_enemy = enemy_priority;
    state->priority_request_active = 0;
    state->enemy_priority_request_active = 0;

    if (copy_map_to_shared(state, map) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    copy_initial_positions(state, map);

    if (initialize_mutexes(state) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (initialize_semaphores(state) != PACMAN_OK) {
        pthread_mutex_destroy(&state->state_mutex);
        pthread_mutex_destroy(&state->priority_mutex);
        pthread_mutex_destroy(&state->collision_mutex);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

void shared_state_print_summary(const shared_state_t *state)
{
    size_t i;

    printf("Memoria compartida inicializada:\n");
    printf("  global_tick=%d max_ticks=%d game_over=%d\n",
           state->global_tick,
           state->max_ticks,
           state->game_over);
    printf("  pacman=(x=%d y=%d) score=%d lives=%d\n",
           state->pacman_x,
           state->pacman_y,
           state->pacman_score,
           state->pacman_lives);

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        printf("  ghost_%zu=(x=%d y=%d)\n", i + 1, state->ghost_x[i], state->ghost_y[i]);
    }

    printf("  collision_detected=%d collision_tick=%d collision_ghost_id=%d\n",
           state->collision_detected,
           state->collision_tick,
           state->collision_ghost_id);
    printf("  prioridad_pacman=%d prioridad_enemy=%d\n",
           state->prioridad_pacman,
           state->prioridad_enemy);
    printf("  pending_priority_pacman=%d active=%d\n",
           state->pending_priority_pacman,
           state->priority_request_active);
    printf("  pending_priority_enemy=%d active=%d\n",
           state->pending_priority_enemy,
           state->enemy_priority_request_active);
    printf("  map_rows=%zu map_cols=%zu\n", state->map_rows, state->map_cols);
    printf("  sync=mutexes_process_shared semaphores_process_shared\n");
}

int shared_memory_release(SharedMemory *shared)
{
    int status = PACMAN_OK;

    if (shared == NULL) {
        return PACMAN_OK;
    }

    if (shared->state != NULL) {
        if (shared->sync_initialized) {
            destroy_sync_objects(shared->state);
            shared->sync_initialized = 0;
        }
        if (munmap(shared->state, sizeof(shared_state_t)) != 0) {
            perror("munmap");
            status = PACMAN_ERROR;
        }
        shared->state = NULL;
    }

    if (shared->fd != -1) {
        if (close(shared->fd) != 0) {
            perror("close");
            status = PACMAN_ERROR;
        }
        shared->fd = -1;
    }

    if (shared->name != NULL) {
        if (shm_unlink(shared->name) != 0 && errno != ENOENT) {
            perror("shm_unlink");
            status = PACMAN_ERROR;
        }
    }

    return status;
}
