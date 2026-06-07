#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/common.h"
#include "../include/map.h"
#include "../include/movement.h"
#include "../include/scheduler.h"
#include "../include/shared.h"

typedef struct {
    const char *file_name;
    const char *label;
    MovementFile moves;
} MovementInput;

static int parse_max_ticks(const char *text, long *max_ticks)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value <= 0 || value > INT_MAX) {
        return PACMAN_ERROR;
    }

    *max_ticks = value;
    return PACMAN_OK;
}

static int load_movements(const char *case_dir, MovementInput inputs[], size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (movement_load_file(case_dir, inputs[i].file_name, &inputs[i].moves) != PACMAN_OK) {
            return PACMAN_ERROR;
        }
    }

    return PACMAN_OK;
}

static void print_initial_positions(const Map *map)
{
    size_t i;

    printf("Posicion inicial de Pac-Man: fila=%d columna=%d\n",
           map->pacman_start.row,
           map->pacman_start.col);

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        printf("Posicion inicial de fantasma %zu: fila=%d columna=%d\n",
               i + 1,
               map->ghost_starts[i].row,
               map->ghost_starts[i].col);
    }
}

static void print_movement_counts(const MovementInput inputs[], size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        printf("Instrucciones leidas de %s: %zu\n",
               inputs[i].label,
               inputs[i].moves.count);
    }
}

int main(int argc, char *argv[])
{
    long max_ticks = 0;
    Map map = {0};
    SharedMemory shared = {0};
    Scheduler scheduler = {0};
    MovementInput movement_inputs[] = {
        {"pacman_moves.txt", "pacman_moves.txt", {0}},
        {"ghost_1_moves.txt", "ghost_1_moves.txt", {0}},
        {"ghost_2_moves.txt", "ghost_2_moves.txt", {0}},
        {"ghost_3_moves.txt", "ghost_3_moves.txt", {0}},
        {"ghost_4_moves.txt", "ghost_4_moves.txt", {0}},
    };
    const size_t movement_count = sizeof(movement_inputs) / sizeof(movement_inputs[0]);

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <case_dir> <max_ticks>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (parse_max_ticks(argv[2], &max_ticks) != PACMAN_OK) {
        fprintf(stderr, "Error: max_ticks debe ser un entero positivo.\n");
        return EXIT_FAILURE;
    }

    if (map_load(argv[1], &map) != PACMAN_OK) {
        map_free(&map);
        return EXIT_FAILURE;
    }

    if (load_movements(argv[1], movement_inputs, movement_count) != PACMAN_OK) {
        map_free(&map);
        return EXIT_FAILURE;
    }

    printf("Max ticks: %ld\n", max_ticks);
    printf("Dimensiones del mapa: filas=%zu columnas=%zu\n", map.rows, map.cols);
    printf("Mapa cargado:\n");
    map_print(&map);
    print_initial_positions(&map);
    print_movement_counts(movement_inputs, movement_count);

    if (shared_memory_create(&shared) != PACMAN_OK) {
        map_free(&map);
        return EXIT_FAILURE;
    }

    if (shared_state_initialize(shared.state, &map, (int)max_ticks) != PACMAN_OK) {
        shared_memory_release(&shared);
        map_free(&map);
        return EXIT_FAILURE;
    }
    shared.sync_initialized = 1;

    shared_state_print_summary(shared.state);

    scheduler_init(&scheduler, shared.state);
    if (scheduler_run_dry(&scheduler) != PACMAN_OK) {
        shared_memory_release(&shared);
        map_free(&map);
        return EXIT_FAILURE;
    }

    if (shared_memory_release(&shared) != PACMAN_OK) {
        map_free(&map);
        return EXIT_FAILURE;
    }

    map_free(&map);
    return EXIT_SUCCESS;
}
