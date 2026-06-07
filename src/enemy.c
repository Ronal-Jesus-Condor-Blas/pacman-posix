#include "../include/enemy.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    ENEMY_INSTRUCTION_MOVE = 0,
    ENEMY_INSTRUCTION_SET_PRIORITY = 1
} EnemyInstructionType;

typedef enum {
    ENEMY_MOVE_UP = 0,
    ENEMY_MOVE_DOWN,
    ENEMY_MOVE_LEFT,
    ENEMY_MOVE_RIGHT
} EnemyMoveDirection;

typedef struct EnemyInstructionNode {
    EnemyInstructionType type;
    EnemyMoveDirection direction;
    int priority;
    char text[64];
    struct EnemyInstructionNode *next;
} EnemyInstructionNode;

typedef struct {
    int x;
    int y;
} EnemyPosition;

typedef struct EnemyContext EnemyContext;

typedef struct {
    int id;
    char file_name[32];
    EnemyInstructionNode *head;
    EnemyInstructionNode *tail;
    EnemyPosition position;
    sem_t turn_requested;
    sem_t turn_done;
    EnemyContext *context;
} GhostContext;

struct EnemyContext {
    shared_state_t *shared;
    const char *case_dir;
    int should_stop;
    pthread_mutex_t stop_mutex;
    pthread_mutex_t ghost_mutex;
    pthread_mutex_t pacman_mutex;
    EnemyPosition pacman_position;
    GhostContext ghosts[PACMAN_GHOST_COUNT];
    sem_t tracker_requested;
    sem_t tracker_done;
    sem_t collision_requested;
    sem_t collision_done;
};

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

static int parse_instruction(const char *line, EnemyInstructionNode *instruction)
{
    const char *priority_prefix = "SET_PRIORITY ";
    const size_t priority_prefix_len = strlen(priority_prefix);

    memset(instruction, 0, sizeof(*instruction));
    snprintf(instruction->text, sizeof(instruction->text), "%s", line);

    if (strcmp(line, "UP") == 0) {
        instruction->type = ENEMY_INSTRUCTION_MOVE;
        instruction->direction = ENEMY_MOVE_UP;
        return PACMAN_OK;
    }
    if (strcmp(line, "DOWN") == 0) {
        instruction->type = ENEMY_INSTRUCTION_MOVE;
        instruction->direction = ENEMY_MOVE_DOWN;
        return PACMAN_OK;
    }
    if (strcmp(line, "LEFT") == 0) {
        instruction->type = ENEMY_INSTRUCTION_MOVE;
        instruction->direction = ENEMY_MOVE_LEFT;
        return PACMAN_OK;
    }
    if (strcmp(line, "RIGHT") == 0) {
        instruction->type = ENEMY_INSTRUCTION_MOVE;
        instruction->direction = ENEMY_MOVE_RIGHT;
        return PACMAN_OK;
    }
    if (strncmp(line, priority_prefix, priority_prefix_len) == 0) {
        instruction->type = ENEMY_INSTRUCTION_SET_PRIORITY;
        return parse_priority_value(line + priority_prefix_len, &instruction->priority);
    }

    return PACMAN_ERROR;
}

static int build_path(char *buffer, size_t buffer_size, const char *case_dir, const char *file_name)
{
    int written = snprintf(buffer, buffer_size, "%s/%s", case_dir, file_name);

    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "[P2] ruta demasiado larga para %s\n", file_name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int append_instruction(GhostContext *ghost, const EnemyInstructionNode *instruction)
{
    EnemyInstructionNode *node = malloc(sizeof(*node));

    if (node == NULL) {
        fprintf(stderr, "[P2] sin memoria para ghost_%d\n", ghost->id);
        return PACMAN_ERROR;
    }

    *node = *instruction;
    node->next = NULL;

    if (ghost->tail == NULL) {
        ghost->head = node;
        ghost->tail = node;
    } else {
        ghost->tail->next = node;
        ghost->tail = node;
    }

    return PACMAN_OK;
}

static void free_instructions(EnemyInstructionNode *node)
{
    while (node != NULL) {
        EnemyInstructionNode *next = node->next;
        free(node);
        node = next;
    }
}

static int load_ghost_instructions(GhostContext *ghost)
{
    char path[4096];
    char line[4096];
    FILE *file;
    size_t line_number = 0;

    if (build_path(path, sizeof(path), ghost->context->case_dir, ghost->file_name) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        perror("[P2] fopen ghost_moves");
        return PACMAN_ERROR;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        EnemyInstructionNode instruction;

        ++line_number;
        if (strip_line(line) == 0) {
            fprintf(stderr, "[P2] ghost_%d instruccion vacia linea %zu\n", ghost->id, line_number);
            continue;
        }

        if (parse_instruction(line, &instruction) != PACMAN_OK) {
            fprintf(stderr, "[P2] ghost_%d instruccion invalida linea %zu: %s\n", ghost->id, line_number, line);
            continue;
        }

        if (append_instruction(ghost, &instruction) != PACMAN_OK) {
            fclose(file);
            return PACMAN_ERROR;
        }
    }

    if (ferror(file)) {
        perror("[P2] fgets ghost_moves");
        fclose(file);
        return PACMAN_ERROR;
    }

    fclose(file);
    return PACMAN_OK;
}

static int pop_instruction(GhostContext *ghost, EnemyInstructionNode *instruction, int *has_instruction)
{
    EnemyInstructionNode *node = ghost->head;

    *has_instruction = 0;
    if (node == NULL) {
        return PACMAN_OK;
    }

    ghost->head = node->next;
    if (ghost->head == NULL) {
        ghost->tail = NULL;
    }

    *instruction = *node;
    instruction->next = NULL;
    free(node);
    *has_instruction = 1;
    return PACMAN_OK;
}

static int is_stop_requested(EnemyContext *context)
{
    int should_stop;

    if (lock_mutex(&context->stop_mutex, "pthread_mutex_lock enemy stop") != PACMAN_OK) {
        return 1;
    }
    should_stop = context->should_stop;
    if (unlock_mutex(&context->stop_mutex, "pthread_mutex_unlock enemy stop") != PACMAN_OK) {
        return 1;
    }

    return should_stop;
}

static int request_stop(EnemyContext *context)
{
    if (lock_mutex(&context->stop_mutex, "pthread_mutex_lock enemy stop") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    context->should_stop = 1;

    if (unlock_mutex(&context->stop_mutex, "pthread_mutex_unlock enemy stop") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int read_game_over(shared_state_t *state, int *value)
{
    if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    *value = state->game_over;

    if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int read_global_tick(shared_state_t *state, int *tick)
{
    if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    *tick = state->global_tick;

    if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static void movement_delta(EnemyMoveDirection direction, int *dx, int *dy)
{
    *dx = 0;
    *dy = 0;

    if (direction == ENEMY_MOVE_UP) {
        *dy = -1;
    } else if (direction == ENEMY_MOVE_DOWN) {
        *dy = 1;
    } else if (direction == ENEMY_MOVE_LEFT) {
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

static int request_enemy_priority_change(shared_state_t *state, int priority)
{
    if (lock_mutex(&state->priority_mutex, "pthread_mutex_lock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    state->pending_priority_enemy = priority;
    state->enemy_priority_request_active = 1;

    if (unlock_mutex(&state->priority_mutex, "pthread_mutex_unlock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("[P2] solicitud SET_PRIORITY enemy=%d\n", priority);
    return PACMAN_OK;
}

static int apply_ghost_move(GhostContext *ghost, EnemyMoveDirection direction)
{
    EnemyContext *context = ghost->context;
    int dx;
    int dy;
    int old_x;
    int old_y;
    int next_x;
    int next_y;

    movement_delta(direction, &dx, &dy);

    if (lock_mutex(&context->ghost_mutex, "pthread_mutex_lock ghost positions") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    old_x = ghost->position.x;
    old_y = ghost->position.y;
    next_x = old_x + dx;
    next_y = old_y + dy;

    if (is_wall_or_out_of_bounds(context->shared, next_x, next_y)) {
        printf("[P2] ghost_%d movimiento invalido contra pared: permanece en (%d,%d)\n",
               ghost->id,
               old_x,
               old_y);
    } else {
        ghost->position.x = next_x;
        ghost->position.y = next_y;
        printf("[P2] ghost_%d movimiento valido: (%d,%d) -> (%d,%d)\n",
               ghost->id,
               old_x,
               old_y,
               next_x,
               next_y);
    }

    return unlock_mutex(&context->ghost_mutex, "pthread_mutex_unlock ghost positions");
}

static int process_ghost_instruction(GhostContext *ghost, const EnemyInstructionNode *instruction)
{
    printf("[P2] ghost_%d instruccion consumida: %s\n", ghost->id, instruction->text);

    if (instruction->type == ENEMY_INSTRUCTION_SET_PRIORITY) {
        return request_enemy_priority_change(ghost->context->shared, instruction->priority);
    }

    return apply_ghost_move(ghost, instruction->direction);
}

static void post_all_ghost_turns(EnemyContext *context)
{
    size_t i;

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        post_semaphore(&context->ghosts[i].turn_requested, "sem_post ghost turn");
    }
}

static int wait_all_ghosts_done(EnemyContext *context)
{
    size_t i;

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        if (wait_semaphore(&context->ghosts[i].turn_done, "sem_wait ghost done") != PACMAN_OK) {
            return PACMAN_ERROR;
        }
    }

    return PACMAN_OK;
}

static int signal_helper_shutdown(EnemyContext *context)
{
    size_t i;

    if (request_stop(context) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        post_semaphore(&context->ghosts[i].turn_requested, "sem_post ghost shutdown");
    }
    post_semaphore(&context->tracker_requested, "sem_post tracker shutdown");
    post_semaphore(&context->collision_requested, "sem_post collision shutdown");

    return PACMAN_OK;
}

static void *ghost_thread(void *arg)
{
    GhostContext *ghost = arg;

    printf("[P2] ghost_thread_%d iniciado\n", ghost->id);

    if (load_ghost_instructions(ghost) != PACMAN_OK) {
        request_stop(ghost->context);
    }

    while (1) {
        EnemyInstructionNode instruction;
        int has_instruction = 0;

        if (wait_semaphore(&ghost->turn_requested, "sem_wait ghost turn") != PACMAN_OK) {
            request_stop(ghost->context);
            break;
        }

        if (is_stop_requested(ghost->context)) {
            break;
        }

        if (pop_instruction(ghost, &instruction, &has_instruction) != PACMAN_OK) {
            request_stop(ghost->context);
            break;
        }

        if (has_instruction) {
            if (process_ghost_instruction(ghost, &instruction) != PACMAN_OK) {
                request_stop(ghost->context);
                break;
            }
        } else {
            printf("[P2] ghost_%d sin instrucciones disponibles para este turno\n", ghost->id);
        }

        if (post_semaphore(&ghost->turn_done, "sem_post ghost done") != PACMAN_OK) {
            request_stop(ghost->context);
            break;
        }
    }

    return NULL;
}

static void *pacman_tracker_thread(void *arg)
{
    EnemyContext *context = arg;

    printf("[P2] pacman_tracker_thread iniciado\n");

    while (1) {
        int x;
        int y;

        if (wait_semaphore(&context->tracker_requested, "sem_wait tracker") != PACMAN_OK) {
            request_stop(context);
            break;
        }

        if (is_stop_requested(context)) {
            break;
        }

        if (lock_mutex(&context->shared->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
            request_stop(context);
            break;
        }
        x = context->shared->pacman_x;
        y = context->shared->pacman_y;
        if (unlock_mutex(&context->shared->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
            request_stop(context);
            break;
        }

        if (lock_mutex(&context->pacman_mutex, "pthread_mutex_lock tracked pacman") != PACMAN_OK) {
            request_stop(context);
            break;
        }
        context->pacman_position.x = x;
        context->pacman_position.y = y;
        if (unlock_mutex(&context->pacman_mutex, "pthread_mutex_unlock tracked pacman") != PACMAN_OK) {
            request_stop(context);
            break;
        }

        if (post_semaphore(&context->tracker_done, "sem_post tracker done") != PACMAN_OK) {
            request_stop(context);
            break;
        }
    }

    return NULL;
}

static int publish_collision(EnemyContext *context, int ghost_id, int tick)
{
    if (lock_mutex(&context->shared->collision_mutex, "pthread_mutex_lock collision_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    context->shared->collision_detected = 1;
    context->shared->collision_tick = tick;
    context->shared->collision_ghost_id = ghost_id;

    if (unlock_mutex(&context->shared->collision_mutex, "pthread_mutex_unlock collision_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("[P2] collision_detected con ghost_%d en tick %d\n", ghost_id, tick);
    return PACMAN_OK;
}

static int detect_collision(EnemyContext *context)
{
    EnemyPosition pacman_position;
    EnemyPosition ghost_positions[PACMAN_GHOST_COUNT];
    int tick;
    size_t i;

    if (lock_mutex(&context->pacman_mutex, "pthread_mutex_lock tracked pacman") != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    pacman_position = context->pacman_position;
    if (unlock_mutex(&context->pacman_mutex, "pthread_mutex_unlock tracked pacman") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (lock_mutex(&context->ghost_mutex, "pthread_mutex_lock ghost positions") != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        ghost_positions[i] = context->ghosts[i].position;
    }
    if (unlock_mutex(&context->ghost_mutex, "pthread_mutex_unlock ghost positions") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (read_global_tick(context->shared, &tick) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        if (ghost_positions[i].x == pacman_position.x && ghost_positions[i].y == pacman_position.y) {
            return publish_collision(context, (int)i + 1, tick);
        }
    }

    return PACMAN_OK;
}

static void *collision_thread(void *arg)
{
    EnemyContext *context = arg;

    printf("[P2] collision_thread iniciado\n");

    while (1) {
        if (wait_semaphore(&context->collision_requested, "sem_wait collision") != PACMAN_OK) {
            request_stop(context);
            break;
        }

        if (is_stop_requested(context)) {
            break;
        }

        if (detect_collision(context) != PACMAN_OK) {
            request_stop(context);
            break;
        }

        if (post_semaphore(&context->collision_done, "sem_post collision done") != PACMAN_OK) {
            request_stop(context);
            break;
        }
    }

    return NULL;
}

static void *enemy_controller_thread(void *arg)
{
    EnemyContext *context = arg;

    printf("[P2] enemy_controller_thread iniciado\n");

    while (1) {
        int game_over_value = 0;

        if (wait_semaphore(&context->shared->sem_enemy_turn, "sem_wait sem_enemy_turn") != PACMAN_OK) {
            signal_helper_shutdown(context);
            return NULL;
        }

        if (read_game_over(context->shared, &game_over_value) != PACMAN_OK || game_over_value) {
            signal_helper_shutdown(context);
            break;
        }

        post_all_ghost_turns(context);
        if (wait_all_ghosts_done(context) != PACMAN_OK) {
            signal_helper_shutdown(context);
            return NULL;
        }

        if (post_semaphore(&context->tracker_requested, "sem_post tracker") != PACMAN_OK ||
            wait_semaphore(&context->tracker_done, "sem_wait tracker done") != PACMAN_OK) {
            signal_helper_shutdown(context);
            return NULL;
        }

        if (post_semaphore(&context->collision_requested, "sem_post collision") != PACMAN_OK ||
            wait_semaphore(&context->collision_done, "sem_wait collision done") != PACMAN_OK) {
            signal_helper_shutdown(context);
            return NULL;
        }

        if (post_semaphore(&context->shared->sem_p2_done, "sem_post sem_p2_done") != PACMAN_OK) {
            signal_helper_shutdown(context);
            return NULL;
        }
    }

    return NULL;
}

static int init_ghost(GhostContext *ghost, EnemyContext *context, int id)
{
    memset(ghost, 0, sizeof(*ghost));
    ghost->id = id;
    ghost->context = context;
    snprintf(ghost->file_name, sizeof(ghost->file_name), "ghost_%d_moves.txt", id);
    ghost->position.x = context->shared->ghost_x[id - 1];
    ghost->position.y = context->shared->ghost_y[id - 1];

    if (sem_init(&ghost->turn_requested, 0, 0) != 0) {
        perror("sem_init ghost turn");
        return PACMAN_ERROR;
    }

    if (sem_init(&ghost->turn_done, 0, 0) != 0) {
        perror("sem_init ghost done");
        sem_destroy(&ghost->turn_requested);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static void destroy_ghost(GhostContext *ghost)
{
    free_instructions(ghost->head);
    sem_destroy(&ghost->turn_requested);
    sem_destroy(&ghost->turn_done);
}

static int context_init(EnemyContext *context, shared_state_t *state, const char *case_dir)
{
    size_t i;

    memset(context, 0, sizeof(*context));
    context->shared = state;
    context->case_dir = case_dir;
    context->pacman_position.x = state->pacman_x;
    context->pacman_position.y = state->pacman_y;

    if (set_errno_from_result(pthread_mutex_init(&context->stop_mutex, NULL), "pthread_mutex_init enemy stop") != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    if (set_errno_from_result(pthread_mutex_init(&context->ghost_mutex, NULL), "pthread_mutex_init ghost positions") != PACMAN_OK) {
        pthread_mutex_destroy(&context->stop_mutex);
        return PACMAN_ERROR;
    }
    if (set_errno_from_result(pthread_mutex_init(&context->pacman_mutex, NULL), "pthread_mutex_init tracked pacman") != PACMAN_OK) {
        pthread_mutex_destroy(&context->ghost_mutex);
        pthread_mutex_destroy(&context->stop_mutex);
        return PACMAN_ERROR;
    }

    if (sem_init(&context->tracker_requested, 0, 0) != 0 ||
        sem_init(&context->tracker_done, 0, 0) != 0 ||
        sem_init(&context->collision_requested, 0, 0) != 0 ||
        sem_init(&context->collision_done, 0, 0) != 0) {
        perror("sem_init enemy context");
        pthread_mutex_destroy(&context->pacman_mutex);
        pthread_mutex_destroy(&context->ghost_mutex);
        pthread_mutex_destroy(&context->stop_mutex);
        return PACMAN_ERROR;
    }

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        if (init_ghost(&context->ghosts[i], context, (int)i + 1) != PACMAN_OK) {
            while (i > 0) {
                --i;
                destroy_ghost(&context->ghosts[i]);
            }
            sem_destroy(&context->collision_done);
            sem_destroy(&context->collision_requested);
            sem_destroy(&context->tracker_done);
            sem_destroy(&context->tracker_requested);
            pthread_mutex_destroy(&context->pacman_mutex);
            pthread_mutex_destroy(&context->ghost_mutex);
            pthread_mutex_destroy(&context->stop_mutex);
            return PACMAN_ERROR;
        }
    }

    return PACMAN_OK;
}

static void context_destroy(EnemyContext *context)
{
    size_t i;

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        destroy_ghost(&context->ghosts[i]);
    }

    sem_destroy(&context->collision_done);
    sem_destroy(&context->collision_requested);
    sem_destroy(&context->tracker_done);
    sem_destroy(&context->tracker_requested);
    pthread_mutex_destroy(&context->pacman_mutex);
    pthread_mutex_destroy(&context->ghost_mutex);
    pthread_mutex_destroy(&context->stop_mutex);
}

static int create_thread(pthread_t *thread, void *(*start_routine)(void *), void *context, const char *name)
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

int enemy_process_run(shared_state_t *state, const char *case_dir)
{
    EnemyContext context;
    pthread_t controller_thread;
    pthread_t ghost_threads[PACMAN_GHOST_COUNT];
    pthread_t tracker_thread;
    pthread_t detector_thread;
    int controller_created = 0;
    int tracker_created = 0;
    int detector_created = 0;
    int ghost_created[PACMAN_GHOST_COUNT] = {0};
    int status = PACMAN_OK;
    size_t i;

    printf("[P2] PID=%ld inicio como enemy_process\n", (long)getpid());

    if (context_init(&context, state, case_dir) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (create_thread(&controller_thread, enemy_controller_thread, &context, "pthread_create enemy_controller_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
        goto cleanup;
    }
    controller_created = 1;

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        if (create_thread(&ghost_threads[i], ghost_thread, &context.ghosts[i], "pthread_create ghost_thread") != PACMAN_OK) {
            status = PACMAN_ERROR;
            goto cleanup;
        }
        ghost_created[i] = 1;
    }

    if (create_thread(&tracker_thread, pacman_tracker_thread, &context, "pthread_create pacman_tracker_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
        goto cleanup;
    }
    tracker_created = 1;

    if (create_thread(&detector_thread, collision_thread, &context, "pthread_create collision_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
        goto cleanup;
    }
    detector_created = 1;

cleanup:
    if (status != PACMAN_OK) {
        signal_helper_shutdown(&context);
        post_semaphore(&state->sem_enemy_turn, "sem_post sem_enemy_turn");
    }

    if (controller_created && join_thread(controller_thread, "pthread_join enemy_controller_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }

    for (i = 0; i < PACMAN_GHOST_COUNT; ++i) {
        if (ghost_created[i] && join_thread(ghost_threads[i], "pthread_join ghost_thread") != PACMAN_OK) {
            status = PACMAN_ERROR;
        }
    }

    if (tracker_created && join_thread(tracker_thread, "pthread_join pacman_tracker_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }
    if (detector_created && join_thread(detector_thread, "pthread_join collision_thread") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }

    if (status == PACMAN_OK) {
        printf("[P2] threads finalizados correctamente\n");
    }

    printf("[P2] Finalizando enemy_process\n");
    context_destroy(&context);
    return status;
}
