#include "../include/movement.h"

#include <stdio.h>
#include <string.h>

#include "../include/common.h"

static int build_path(char *buffer, size_t buffer_size, const char *case_dir, const char *file_name)
{
    int written = snprintf(buffer, buffer_size, "%s/%s", case_dir, file_name);

    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "Error: ruta demasiado larga para %s.\n", file_name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static size_t strip_line(char *line)
{
    size_t length = strlen(line);
    size_t start = 0;

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' ||
                          line[length - 1] == ' ' || line[length - 1] == '\t')) {
        line[length - 1] = '\0';
        --length;
    }

    while (line[start] == ' ' || line[start] == '\t') {
        ++start;
    }

    if (start > 0) {
        memmove(line, line + start, length - start + 1);
        length -= start;
    }

    return length;
}

static int is_digits(const char *text)
{
    size_t i;

    if (text[0] == '\0') {
        return 0;
    }

    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
    }

    return 1;
}

static int is_valid_instruction(const char *line)
{
    const char *priority_prefix = "SET_PRIORITY ";
    const size_t priority_prefix_len = strlen(priority_prefix);

    if (strcmp(line, "UP") == 0 || strcmp(line, "DOWN") == 0 ||
        strcmp(line, "LEFT") == 0 || strcmp(line, "RIGHT") == 0) {
        return 1;
    }

    if (strncmp(line, priority_prefix, priority_prefix_len) == 0) {
        return is_digits(line + priority_prefix_len);
    }

    return 0;
}

int movement_load_file(const char *case_dir, const char *file_name, MovementFile *moves)
{
    char path[4096];
    char line[4096];
    FILE *file;
    size_t line_number = 0;

    moves->count = 0;

    if (build_path(path, sizeof(path), case_dir, file_name) != PACMAN_OK) {
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
        length = strip_line(line);
        if (length == 0) {
            fprintf(stderr, "Error: %s contiene una instruccion vacia en la linea %zu.\n", file_name, line_number);
            fclose(file);
            return PACMAN_ERROR;
        }

        if (!is_valid_instruction(line)) {
            fprintf(stderr,
                    "Error: instruccion invalida en %s linea %zu: %s\n",
                    file_name,
                    line_number,
                    line);
            fclose(file);
            return PACMAN_ERROR;
        }

        ++moves->count;
    }

    if (ferror(file)) {
        fprintf(stderr, "Error: fallo al leer %s.\n", path);
        fclose(file);
        return PACMAN_ERROR;
    }

    fclose(file);
    return PACMAN_OK;
}
