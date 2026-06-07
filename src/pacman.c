#include "../include/pacman.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    PACMAN_INSTRUCTION_MOVE = 0,
    PACMAN_INSTRUCTION_SET_PRIORITY = 1
} PacmanInstructionType;

typedef enum {
    PACMAN_MOVE_UP = 0,
    PACMAN_MOVE_DOWN,
    PACMAN_MOVE_LEFT,
    PACMAN_MOVE_RIGHT
} PacmanMoveDirection;

typedef struct PacmanInstructionNode {
    PacmanInstructionType type;
    PacmanMoveDirection direction;
    int priority;
    char text[64];
    struct PacmanInstructionNode *next;
} PacmanInstructionNode;

typedef struct {
    PacmanInstructionNode *head;
    PacmanInstructionNode *tail;
    size_t count;
    int reader_done;
    int should_stop;
    pthread_mutex_t mutex;
    sem_t available;
} PacmanInstructionQueue;

typedef struct {
    int x;
    int y;
    int score;
    pthread_mutex_t mutex;
} PacmanLocalState;

typedef struct {
    shared_state_t *shared;
    const char *case_dir;
    PacmanInstructionQueue queue;
    PacmanLocalState local;
    sem_t publish_requested;
} PacmanContext;

static int set_errno_from_result(int result, const char *name)
{
    if (result != 0) {
        errno = result;
        perror(name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int lock_mutex(pthread_mutex_t *mutex, const char *name)
{
    return set_errno_from_result(pthread_mutex_lock(mutex), name);
}

static int unlock_mutex(pthread_mutex_t *mutex, const char *name)
{
    return set_errno_from_result(pthread_mutex_unlock(mutex), name);
}

static int wait_semaphore(sem_t *semaphore, const char *name)
{
    while (sem_wait(semaphore) != 0) {
        if (errno != EINTR) {
            perror(name);
            return PACMAN_ERROR;
        }
    }

    return PACMAN_OK;
}

static int post_semaphore(sem_t *semaphore, const char *name)
{
    if (sem_post(semaphore) != 0) {
        perror(name);
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

static int build_path(char *buffer, size_t buffer_size, const char *case_dir)
{
    int written = snprintf(buffer, buffer_size, "%s/pacman_moves.txt", case_dir);

    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "[P1] ruta demasiado larga para pacman_moves.txt\n");
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int parse_priority_value(const char *text, int *priority)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < MIN_PRIORITY || value > MAX_PRIORITY) {
        return PACMAN_ERROR;
    }

    *priority = (int)value;
    return PACMAN_OK;
}

static int parse_instruction(const char *line, PacmanInstructionNode *instruction)
{
    const char *priority_prefix = "SET_PRIORITY ";
    const size_t priority_prefix_len = strlen(priority_prefix);

    memset(instruction, 0, sizeof(*instruction));
    snprintf(instruction->text, sizeof(instruction->text), "%s", line);

    if (strcmp(line, "UP") == 0) {
        instruction->type = PACMAN_INSTRUCTION_MOVE;
        instruction->direction = PACMAN_MOVE_UP;
        return PACMAN_OK;
    }
    if (strcmp(line, "DOWN") == 0) {
        instruction->type = PACMAN_INSTRUCTION_MOVE;
        instruction->direction = PACMAN_MOVE_DOWN;
        return PACMAN_OK;
    }
    if (strcmp(line, "LEFT") == 0) {
        instruction->type = PACMAN_INSTRUCTION_MOVE;
        instruction->direction = PACMAN_MOVE_LEFT;
        return PACMAN_OK;
    }
    if (strcmp(line, "RIGHT") == 0) {
        instruction->type = PACMAN_INSTRUCTION_MOVE;
        instruction->direction = PACMAN_MOVE_RIGHT;
        return PACMAN_OK;
    }
    if (strncmp(line, priority_prefix, priority_prefix_len) == 0) {
        instruction->type = PACMAN_INSTRUCTION_SET_PRIORITY;
        return parse_priority_value(line + priority_prefix_len, &instruction->priority);
    }

    return PACMAN_ERROR;
}

static int queue_init(PacmanInstructionQueue *queue)
{
    memset(queue, 0, sizeof(*queue));

    if (set_errno_from_result(pthread_mutex_init(&queue->mutex, NULL), "pthread_mutex_init queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (sem_init(&queue->available, 0, 0) != 0) {
        perror("sem_init queue");
        pthread_mutex_destroy(&queue->mutex);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static void free_instruction_list(PacmanInstructionNode *node)
{
    while (node != NULL) {
        PacmanInstructionNode *next = node->next;
        free(node);
        node = next;
    }
}

static void queue_destroy(PacmanInstructionQueue *queue)
{
    free_instruction_list(queue->head);
    sem_destroy(&queue->available);
    pthread_mutex_destroy(&queue->mutex);
}

static int queue_push(PacmanInstructionQueue *queue, const PacmanInstructionNode *instruction)
{
    PacmanInstructionNode *node = malloc(sizeof(*node));

    if (node == NULL) {
        fprintf(stderr, "[P1] sin memoria para cola de instrucciones\n");
        return PACMAN_ERROR;
    }

    *node = *instruction;
    node->next = NULL;

    if (lock_mutex(&queue->mutex, "pthread_mutex_lock queue") != PACMAN_OK) {
        free(node);
        return PACMAN_ERROR;
    }

    if (queue->tail == NULL) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    ++queue->count;

    if (unlock_mutex(&queue->mutex, "pthread_mutex_unlock queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return post_semaphore(&queue->available, "sem_post queue");
}

static int queue_pop_locked(PacmanInstructionQueue *queue, PacmanInstructionNode *instruction)
{
    PacmanInstructionNode *node = queue->head;

    if (node == NULL) {
        return PACMAN_ERROR;
    }

    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    --queue->count;
    *instruction = *node;
    instruction->next = NULL;
    free(node);
    return PACMAN_OK;
}

static int queue_pop(PacmanInstructionQueue *queue, PacmanInstructionNode *instruction, int *has_instruction)
{
    *has_instruction = 0;

    while (sem_trywait(&queue->available) != 0) {
        if (errno == EAGAIN) {
            int reader_done;

            if (lock_mutex(&queue->mutex, "pthread_mutex_lock queue") != PACMAN_OK) {
                return PACMAN_ERROR;
            }
            reader_done = queue->reader_done || queue->should_stop;
            if (unlock_mutex(&queue->mutex, "pthread_mutex_unlock queue") != PACMAN_OK) {
                return PACMAN_ERROR;
            }

            if (reader_done) {
                return PACMAN_OK;
            }

            if (wait_semaphore(&queue->available, "sem_wait queue") != PACMAN_OK) {
                return PACMAN_ERROR;
            }
            break;
        }

        if (errno != EINTR) {
            perror("sem_trywait queue");
            return PACMAN_ERROR;
        }
    }

    if (lock_mutex(&queue->mutex, "pthread_mutex_lock queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (queue_pop_locked(queue, instruction) == PACMAN_OK) {
        *has_instruction = 1;
    }

    if (unlock_mutex(&queue->mutex, "pthread_mutex_unlock queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int queue_mark_reader_done(PacmanInstructionQueue *queue)
{
    if (lock_mutex(&queue->mutex, "pthread_mutex_lock queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    queue->reader_done = 1;

    if (unlock_mutex(&queue->mutex, "pthread_mutex_unlock queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return post_semaphore(&queue->available, "sem_post queue");
}

static int queue_request_stop(PacmanInstructionQueue *queue)
{
    if (lock_mutex(&queue->mutex, "pthread_mutex_lock queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    queue->should_stop = 1;

    if (unlock_mutex(&queue->mutex, "pthread_mutex_unlock queue") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return post_semaphore(&queue->available, "sem_post queue");
}

static int read_game_over(shared_state_t *state, int *game_over)
{
    if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    *game_over = state->game_over;

    if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int request_priority_change(shared_state_t *state, int priority)
{
    if (lock_mutex(&state->priority_mutex, "pthread_mutex_lock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    state->pending_priority_pacman = priority;
    state->priority_request_active = 1;

    if (unlock_mutex(&state->priority_mutex, "pthread_mutex_unlock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("[P1] solicitud SET_PRIORITY=%d\n", priority);
    return PACMAN_OK;
}

static void movement_delta(PacmanMoveDirection direction, int *dx, int *dy)
{
    *dx = 0;
    *dy = 0;

    if (direction == PACMAN_MOVE_UP) {
        *dy = -1;
    } else if (direction == PACMAN_MOVE_DOWN) {
        *dy = 1;
    } else if (direction == PACMAN_MOVE_LEFT) {
        *dx = -1;
    } else {
        *dx = 1;
    }
}

static int is_wall_or_out_of_bounds(shared_state_t *state, int x, int y)
{
    if (x < 0 || y < 0 || (size_t)y >= state->map_rows || (size_t)x >= state->map_cols) {
        return 1;
    }

    return state->map_grid[y][x] == 'X';
}

static int apply_move(PacmanContext *context, PacmanMoveDirection direction)
{
    int dx;
    int dy;
    int old_x;
    int old_y;
    int next_x;
    int next_y;

    movement_delta(direction, &dx, &dy);

    if (lock_mutex(&context->local.mutex, "pthread_mutex_lock pacman local") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    old_x = context->local.x;
    old_y = context->local.y;
    next_x = old_x + dx;
    next_y = old_y + dy;

    if (is_wall_or_out_of_bounds(context->shared, next_x, next_y)) {
        printf("[P1] movimiento invalido contra pared: permanece en (%d,%d)\n", old_x, old_y);
    } else {
        context->local.x = next_x;
        context->local.y = next_y;
        printf("[P1] movimiento valido: (%d,%d) -> (%d,%d)\n", old_x, old_y, next_x, next_y);
    }

    return unlock_mutex(&context->local.mutex, "pthread_mutex_unlock pacman local");
}

static int process_instruction(PacmanContext *context, const PacmanInstructionNode *instruction)
{
    printf("[P1] instruccion consumida: %s\n", instruction->text);

    if (instruction->type == PACMAN_INSTRUCTION_SET_PRIORITY) {
        return request_priority_change(context->shared, instruction->priority);
    }

    return apply_move(context, instruction->direction);
}

static void *movement_reader_thread(void *arg)
{
    PacmanContext *context = arg;
    char path[4096];
    char line[4096];
    FILE *file;
    size_t line_number = 0;

    printf("[P1] movement_reader_thread iniciado\n");

    if (build_path(path, sizeof(path), context->case_dir) != PACMAN_OK) {
        queue_mark_reader_done(&context->queue);
        return NULL;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        perror("[P1] fopen pacman_moves.txt");
        queue_mark_reader_done(&context->queue);
        return NULL;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        PacmanInstructionNode instruction;

        ++line_number;
        if (strip_line(line) == 0) {
            fprintf(stderr, "[P1] instruccion vacia en pacman_moves.txt linea %zu\n", line_number);
            continue;
        }

        if (parse_instruction(line, &instruction) != PACMAN_OK) {
            fprintf(stderr, "[P1] instruccion invalida en pacman_moves.txt linea %zu: %s\n", line_number, line);
            continue;
        }

        if (queue_push(&context->queue, &instruction) != PACMAN_OK) {
            break;
        }
    }

    if (ferror(file)) {
        perror("[P1] fgets pacman_moves.txt");
    }

    fclose(file);
    queue_mark_reader_done(&context->queue);
    return NULL;
}

static void *movement_executor_thread(void *arg)
{
    PacmanContext *context = arg;

    printf("[P1] movement_executor_thread iniciado\n");

    while (1) {
        int game_over = 0;
        PacmanInstructionNode instruction;
        int has_instruction = 0;

        if (wait_semaphore(&context->shared->sem_pacman_turn, "sem_wait sem_pacman_turn") != PACMAN_OK) {
            queue_request_stop(&context->queue);
            post_semaphore(&context->publish_requested, "sem_post publish_requested");
            return NULL;
        }

        if (read_game_over(context->shared, &game_over) != PACMAN_OK || game_over) {
            queue_request_stop(&context->queue);
            post_semaphore(&context->publish_requested, "sem_post publish_requested");
            break;
        }

        if (queue_pop(&context->queue, &instruction, &has_instruction) != PACMAN_OK) {
            queue_request_stop(&context->queue);
            post_semaphore(&context->publish_requested, "sem_post publish_requested");
            return NULL;
        }

        if (has_instruction) {
            if (process_instruction(context, &instruction) != PACMAN_OK) {
                queue_request_stop(&context->queue);
                post_semaphore(&context->publish_requested, "sem_post publish_requested");
                return NULL;
            }
        } else {
            printf("[P1] sin instrucciones disponibles para este turno\n");
        }

        if (post_semaphore(&context->publish_requested, "sem_post publish_requested") != PACMAN_OK) {
            queue_request_stop(&context->queue);
            return NULL;
        }
    }

    return NULL;
}

static void *pacman_publisher_thread(void *arg)
{
    PacmanContext *context = arg;

    printf("[P1] pacman_publisher_thread iniciado\n");

    while (1) {
        int game_over = 0;
        int x;
        int y;
        int score;

        if (wait_semaphore(&context->publish_requested, "sem_wait publish_requested") != PACMAN_OK) {
            return NULL;
        }

        if (read_game_over(context->shared, &game_over) != PACMAN_OK) {
            return NULL;
        }

        if (game_over) {
            break;
        }

        if (lock_mutex(&context->local.mutex, "pthread_mutex_lock pacman local") != PACMAN_OK) {
            return NULL;
        }
        x = context->local.x;
        y = context->local.y;
        score = context->local.score;
        if (unlock_mutex(&context->local.mutex, "pthread_mutex_unlock pacman local") != PACMAN_OK) {
            return NULL;
        }

        if (lock_mutex(&context->shared->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
            return NULL;
        }
        context->shared->pacman_x = x;
        context->shared->pacman_y = y;
        context->shared->pacman_score = score;
        if (unlock_mutex(&context->shared->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
            return NULL;
        }

        printf("[P1] estado publicado: pacman=(%d,%d) score=%d\n", x, y, score);

        if (post_semaphore(&context->shared->sem_p1_done, "sem_post sem_p1_done") != PACMAN_OK) {
            return NULL;
        }
    }

    return NULL;
}

static int context_init(PacmanContext *context, shared_state_t *state, const char *case_dir)
{
    memset(context, 0, sizeof(*context));
    context->shared = state;
    context->case_dir = case_dir;
    context->local.x = state->pacman_x;
    context->local.y = state->pacman_y;
    context->local.score = state->pacman_score;

    if (queue_init(&context->queue) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (set_errno_from_result(pthread_mutex_init(&context->local.mutex, NULL),
                              "pthread_mutex_init pacman local") != PACMAN_OK) {
        queue_destroy(&context->queue);
        return PACMAN_ERROR;
    }

    if (sem_init(&context->publish_requested, 0, 0) != 0) {
        perror("sem_init publish_requested");
        pthread_mutex_destroy(&context->local.mutex);
        queue_destroy(&context->queue);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static void context_destroy(PacmanContext *context)
{
    sem_destroy(&context->publish_requested);
    pthread_mutex_destroy(&context->local.mutex);
    queue_destroy(&context->queue);
}

static int create_thread(pthread_t *thread, void *(*start_routine)(void *), PacmanContext *context, const char *name)
{
    int result = pthread_create(thread, NULL, start_routine, context);

    if (result != 0) {
        errno = result;
        perror(name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int join_thread(pthread_t thread, const char *name)
{
    int result = pthread_join(thread, NULL);

    if (result != 0) {
        errno = result;
        perror(name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

int pacman_process_run(shared_state_t *state, const char *case_dir)
{
    PacmanContext context;
    pthread_t reader_thread;
    pthread_t executor_thread;
    pthread_t publisher_thread;
    int reader_created = 0;
    int executor_created = 0;
    int publisher_created = 0;
    int status = PACMAN_OK;

    printf("[P1] PID=%ld inicio como pacman_process\n", (long)getpid());

    if (context_init(&context, state, case_dir) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (create_thread(&reader_thread, movement_reader_thread, &context, "pthread_create movement_reader_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
        goto cleanup;
    }
    reader_created = 1;

    if (create_thread(&executor_thread, movement_executor_thread, &context, "pthread_create movement_executor_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
        goto cleanup;
    }
    executor_created = 1;

    if (create_thread(&publisher_thread, pacman_publisher_thread, &context, "pthread_create pacman_publisher_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
        goto cleanup;
    }
    publisher_created = 1;

cleanup:
    if (status != PACMAN_OK) {
        queue_request_stop(&context.queue);
        post_semaphore(&state->sem_pacman_turn, "sem_post sem_pacman_turn");
        post_semaphore(&context.publish_requested, "sem_post publish_requested");
    }

    if (reader_created && join_thread(reader_thread, "pthread_join movement_reader_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }
    if (executor_created && join_thread(executor_thread, "pthread_join movement_executor_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }
    if (publisher_created && join_thread(publisher_thread, "pthread_join pacman_publisher_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }

    if (status == PACMAN_OK) {
        printf("[P1] threads finalizados correctamente\n");
    }

    printf("[P1] Finalizando pacman_process\n");
    context_destroy(&context);
    return status;
}
