#include "../include/map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int build_path(char *buffer, size_t buffer_size, const char *case_dir, const char *file_name)
{
    int written = snprintf(buffer, buffer_size, "%s/%s", case_dir, file_name);

    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "Error: ruta demasiado larga para %s.\n", file_name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static char *duplicate_line(const char *line, size_t length)
{
    char *copy = malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, line, length);
    copy[length] = '\0';
    return copy;
}

static size_t strip_line_ending(char *line)
{
    size_t length = strlen(line);

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        --length;
    }

    return length;
}

static int append_row(Map *map, const char *line, size_t length)
{
    char **new_cells;
    char *row_copy;

    row_copy = duplicate_line(line, length);
    if (row_copy == NULL) {
        fprintf(stderr, "Error: no hay memoria suficiente para cargar el mapa.\n");
        return PACMAN_ERROR;
    }

    new_cells = realloc(map->cells, (map->rows + 1) * sizeof(map->cells[0]));
    if (new_cells == NULL) {
        free(row_copy);
        fprintf(stderr, "Error: no hay memoria suficiente para cargar el mapa.\n");
        return PACMAN_ERROR;
    }

    map->cells = new_cells;
    map->cells[map->rows] = row_copy;
    ++map->rows;
    return PACMAN_OK;
}

static int validate_row_width(Map *map, size_t length, size_t line_number)
{
    if (length == 0) {
        fprintf(stderr, "Error: map.txt contiene una linea vacia en la linea %zu.\n", line_number);
        return PACMAN_ERROR;
    }

    if (map->rows == 0) {
        map->cols = length;
        return PACMAN_OK;
    }

    if (length != map->cols) {
        fprintf(stderr,
                "Error: map.txt no es rectangular en la linea %zu (esperado %zu, recibido %zu).\n",
                line_number,
                map->cols,
                length);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int ghost_index_for_cell(char cell)
{
    if (cell >= 'A' && cell <= 'D') {
        return cell - 'A';
    }

    return -1;
}

static int validate_map_cell(char cell, size_t row, size_t col)
{
    if (cell == 'X' || cell == 'O' || cell == 'P' || ghost_index_for_cell(cell) >= 0) {
        return PACMAN_OK;
    }

    fprintf(stderr,
            "Error: caracter invalido '%c' en map.txt fila=%zu columna=%zu.\n",
            cell,
            row,
            col);
    return PACMAN_ERROR;
}

static int register_entity(char cell, size_t row, size_t col, Map *map, int *pacman_count, int ghost_counts[])
{
    int ghost_index;

    if (cell == 'P') {
        ++(*pacman_count);
        map->pacman_start.row = (int)row;
        map->pacman_start.col = (int)col;
        return PACMAN_OK;
    }

    ghost_index = ghost_index_for_cell(cell);
    if (ghost_index >= 0) {
        ++ghost_counts[ghost_index];
        map->ghost_starts[ghost_index].row = (int)row;
        map->ghost_starts[ghost_index].col = (int)col;
    }

    return PACMAN_OK;
}

static int validate_entities(const Map *map)
{
    size_t row;
    size_t col;
    int pacman_count = 0;
    int ghost_counts[PACMAN_GHOST_COUNT] = {0};
    Map mutable_map = *map;

    for (row = 0; row < map->rows; ++row) {
        for (col = 0; col < map->cols; ++col) {
            char cell = map->cells[row][col];

            if (validate_map_cell(cell, row, col) != PACMAN_OK) {
                return PACMAN_ERROR;
            }

            if (register_entity(cell, row, col, &mutable_map, &pacman_count, ghost_counts) != PACMAN_OK) {
                return PACMAN_ERROR;
            }
        }
    }

    if (pacman_count != 1) {
        fprintf(stderr, "Error: map.txt debe contener exactamente un Pac-Man P (encontrados %d).\n", pacman_count);
        return PACMAN_ERROR;
    }

    for (row = 0; row < PACMAN_GHOST_COUNT; ++row) {
        if (ghost_counts[row] != 1) {
            fprintf(stderr,
                    "Error: map.txt debe contener exactamente un fantasma %c (encontrados %d).\n",
                    (char)('A' + row),
                    ghost_counts[row]);
            return PACMAN_ERROR;
        }
    }

    ((Map *)map)->pacman_start = mutable_map.pacman_start;
    for (row = 0; row < PACMAN_GHOST_COUNT; ++row) {
        ((Map *)map)->ghost_starts[row] = mutable_map.ghost_starts[row];
    }

    return PACMAN_OK;
}

int map_load(const char *case_dir, Map *map)
{
    char path[4096];
    char line[4096];
    FILE *file;
    size_t line_number = 0;

    if (build_path(path, sizeof(path), case_dir, "map.txt") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: no se pudo abrir %s.\n", path);
        return PACMAN_ERROR;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length;

        ++line_number;
        length = strip_line_ending(line);

        if (validate_row_width(map, length, line_number) != PACMAN_OK ||
            append_row(map, line, length) != PACMAN_OK) {
            fclose(file);
            return PACMAN_ERROR;
        }
    }

    if (ferror(file)) {
        fprintf(stderr, "Error: fallo al leer %s.\n", path);
        fclose(file);
        return PACMAN_ERROR;
    }

    fclose(file);

    if (map->rows == 0) {
        fprintf(stderr, "Error: map.txt esta vacio.\n");
        return PACMAN_ERROR;
    }

    return validate_entities(map);
}

void map_print(const Map *map)
{
    size_t row;

    for (row = 0; row < map->rows; ++row) {
        printf("%s\n", map->cells[row]);
    }
}

void map_free(Map *map)
{
    size_t row;

    if (map == NULL) {
        return;
    }

    for (row = 0; row < map->rows; ++row) {
        free(map->cells[row]);
    }

    free(map->cells);
    map->cells = NULL;
    map->rows = 0;
    map->cols = 0;
}
