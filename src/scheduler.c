#include "../include/scheduler.h"

#include <errno.h>
#include <stdio.h>

static int lock_mutex(pthread_mutex_t *mutex, const char *name)
{
    int result = pthread_mutex_lock(mutex);

    if (result != 0) {
        errno = result;
        perror(name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int unlock_mutex(pthread_mutex_t *mutex, const char *name)
{
    int result = pthread_mutex_unlock(mutex);

    if (result != 0) {
        errno = result;
        perror(name);
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int wait_done(sem_t *done_sem)
{
    while (sem_wait(done_sem) != 0) {
        if (errno != EINTR) {
            perror("sem_wait");
            return PACMAN_ERROR;
        }
    }

    return PACMAN_OK;
}

static int post_turn(sem_t *turn_sem)
{
    if (sem_post(turn_sem) != 0) {
        perror("sem_post");
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int dispatch_selected_process(shared_state_t *state, SchedulerProcess selected)
{
    if (selected == SCHEDULER_PROCESS_PACMAN) {
        if (post_turn(&state->sem_pacman_turn) != PACMAN_OK) {
            return PACMAN_ERROR;
        }
        if (wait_done(&state->sem_p1_done) != PACMAN_OK) {
            return PACMAN_ERROR;
        }
        printf("[P0] P1 confirmo fin de turno\n");
        return PACMAN_OK;
    }

    if (post_turn(&state->sem_enemy_turn) != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    if (wait_done(&state->sem_p2_done) != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    printf("[P0] P2 confirmo fin de turno\n");
    return PACMAN_OK;
}

static int is_priority_in_range(int priority)
{
    return priority >= MIN_PRIORITY && priority <= MAX_PRIORITY;
}

static void apply_pacman_priority_request(shared_state_t *state)
{
    if (!state->priority_request_active) {
        return;
    }

    if (is_priority_in_range(state->pending_priority_pacman)) {
        state->prioridad_pacman = state->pending_priority_pacman;
        printf("[scheduler] solicitud prioridad Pac-Man aplicada: %d\n", state->prioridad_pacman);
    } else {
        printf("[scheduler] solicitud prioridad Pac-Man rechazada: %d fuera de rango [%d, %d]\n",
               state->pending_priority_pacman,
               MIN_PRIORITY,
               MAX_PRIORITY);
    }

    state->priority_request_active = 0;
}

static void apply_enemy_priority_request(shared_state_t *state)
{
    if (!state->enemy_priority_request_active) {
        return;
    }

    if (is_priority_in_range(state->pending_priority_enemy)) {
        state->prioridad_enemy = state->pending_priority_enemy;
        printf("[scheduler] solicitud prioridad Enemy aplicada: %d\n", state->prioridad_enemy);
    } else {
        printf("[scheduler] solicitud prioridad Enemy rechazada: %d fuera de rango [%d, %d]\n",
               state->pending_priority_enemy,
               MIN_PRIORITY,
               MAX_PRIORITY);
    }

    state->enemy_priority_request_active = 0;
}

static SchedulerProcess select_by_priority_or_round_robin(Scheduler *scheduler)
{
    shared_state_t *state = scheduler->state;

    if (state->prioridad_pacman > state->prioridad_enemy) {
        return SCHEDULER_PROCESS_PACMAN;
    }

    if (state->prioridad_enemy > state->prioridad_pacman) {
        return SCHEDULER_PROCESS_ENEMY;
    }

    if (scheduler->last_round_robin == SCHEDULER_PROCESS_PACMAN) {
        scheduler->last_round_robin = SCHEDULER_PROCESS_ENEMY;
    } else {
        scheduler->last_round_robin = SCHEDULER_PROCESS_PACMAN;
    }

    return scheduler->last_round_robin;
}

void scheduler_init(Scheduler *scheduler, shared_state_t *state)
{
    scheduler->state = state;
    scheduler->last_round_robin = SCHEDULER_PROCESS_ENEMY;
    scheduler->last_processed_collision_tick = -1;
}

static int set_game_over(shared_state_t *state)
{
    if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    state->game_over = 1;

    if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
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

static int decrement_life_for_collision(shared_state_t *state, int *remaining_lives)
{
    if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (state->pacman_lives > 0) {
        --state->pacman_lives;
    }
    *remaining_lives = state->pacman_lives;

    if (state->pacman_lives <= 0) {
        state->game_over = 1;
    }

    if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int clear_collision_event(shared_state_t *state)
{
    state->collision_detected = 0;
    state->collision_tick = -1;
    state->collision_ghost_id = -1;
    return PACMAN_OK;
}

int scheduler_process_collision_events(Scheduler *scheduler)
{
    shared_state_t *state = scheduler->state;
    int collision_detected;
    int collision_tick;
    int collision_ghost_id;
    int should_process = 0;

    if (lock_mutex(&state->collision_mutex, "pthread_mutex_lock collision_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    collision_detected = state->collision_detected;
    collision_tick = state->collision_tick;
    collision_ghost_id = state->collision_ghost_id;

    if (collision_detected && collision_tick != scheduler->last_processed_collision_tick) {
        scheduler->last_processed_collision_tick = collision_tick;
        should_process = 1;
    }

    if (collision_detected) {
        clear_collision_event(state);
    }

    if (unlock_mutex(&state->collision_mutex, "pthread_mutex_unlock collision_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (should_process) {
        int remaining_lives = 0;

        printf("[P0] colision detectada en tick %d con ghost_%d\n", collision_tick, collision_ghost_id);

        if (decrement_life_for_collision(state, &remaining_lives) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        printf("[P0] vidas restantes=%d\n", remaining_lives);

        if (remaining_lives <= 0) {
            printf("[P0] Pac-Man sin vidas. game_over=1\n");
        }
    }

    return PACMAN_OK;
}

int scheduler_apply_priority_requests(Scheduler *scheduler)
{
    shared_state_t *state = scheduler->state;

    if (lock_mutex(&state->priority_mutex, "pthread_mutex_lock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    apply_pacman_priority_request(state);
    apply_enemy_priority_request(state);

    if (unlock_mutex(&state->priority_mutex, "pthread_mutex_unlock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

int scheduler_select_next_process(Scheduler *scheduler, SchedulerProcess *selected)
{
    shared_state_t *state = scheduler->state;

    if (lock_mutex(&state->priority_mutex, "pthread_mutex_lock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    *selected = select_by_priority_or_round_robin(scheduler);

    if (unlock_mutex(&state->priority_mutex, "pthread_mutex_unlock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

int scheduler_run_dry(Scheduler *scheduler)
{
    shared_state_t *state = scheduler->state;

    printf("Scheduler P0 dry-run iniciado.\n");

    while (1) {
        SchedulerProcess selected;

        if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (state->global_tick >= state->max_ticks) {
            if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
                return PACMAN_ERROR;
            }
            break;
        }

        ++state->global_tick;

        if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (scheduler_apply_priority_requests(scheduler) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (scheduler_select_next_process(scheduler, &selected) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (scheduler_print_tick_log(state, selected) != PACMAN_OK) {
            return PACMAN_ERROR;
        }
    }

    printf("Scheduler P0 dry-run finalizado.\n");
    return PACMAN_OK;
}

int scheduler_run(Scheduler *scheduler)
{
    shared_state_t *state = scheduler->state;

    printf("[P0] Scheduler P0 iniciado.\n");

    while (1) {
        SchedulerProcess selected;
        int game_over;

        if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (state->game_over) {
            if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
                return PACMAN_ERROR;
            }
            break;
        }

        if (state->global_tick >= state->max_ticks) {
            if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
                return PACMAN_ERROR;
            }
            if (set_game_over(state) != PACMAN_OK) {
                return PACMAN_ERROR;
            }
            printf("[P0] max_ticks alcanzado. game_over=1\n");
            break;
        }

        ++state->global_tick;

        if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (scheduler_apply_priority_requests(scheduler) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (scheduler_select_next_process(scheduler, &selected) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (scheduler_print_tick_log(state, selected) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (dispatch_selected_process(state, selected) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (scheduler_process_collision_events(scheduler) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (read_game_over(state, &game_over) != PACMAN_OK) {
            return PACMAN_ERROR;
        }
        if (game_over) {
            break;
        }
    }

    if (scheduler_request_shutdown(scheduler) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (scheduler_print_final_summary(state) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("[P0] Scheduler P0 finalizado.\n");
    return PACMAN_OK;
}

int scheduler_request_shutdown(Scheduler *scheduler)
{
    shared_state_t *state = scheduler->state;

    if (set_game_over(state) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("[P0] game_over=1, esperando hijos...\n");

    if (post_turn(&state->sem_pacman_turn) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (post_turn(&state->sem_enemy_turn) != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

int scheduler_print_final_summary(shared_state_t *state)
{
    int global_tick;
    int pacman_x;
    int pacman_y;
    int pacman_score;
    int pacman_lives;
    int game_over;

    if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    global_tick = state->global_tick;
    pacman_x = state->pacman_x;
    pacman_y = state->pacman_y;
    pacman_score = state->pacman_score;
    pacman_lives = state->pacman_lives;
    game_over = state->game_over;

    if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("[P0] Resumen final:\n");
    printf("  ticks ejecutados=%d\n", global_tick);
    printf("  pacman=(%d,%d)\n", pacman_x, pacman_y);
    printf("  score=%d\n", pacman_score);
    printf("  lives=%d\n", pacman_lives);
    printf("  game_over=%d\n", game_over);

    return PACMAN_OK;
}

int scheduler_print_tick_log(shared_state_t *state, SchedulerProcess selected)
{
    int global_tick;
    int max_ticks;
    int pacman_priority;
    int enemy_priority;

    if (lock_mutex(&state->state_mutex, "pthread_mutex_lock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    global_tick = state->global_tick;
    max_ticks = state->max_ticks;
    if (unlock_mutex(&state->state_mutex, "pthread_mutex_unlock state_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    if (lock_mutex(&state->priority_mutex, "pthread_mutex_lock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }
    pacman_priority = state->prioridad_pacman;
    enemy_priority = state->prioridad_enemy;
    if (unlock_mutex(&state->priority_mutex, "pthread_mutex_unlock priority_mutex") != PACMAN_OK) {
        return PACMAN_ERROR;
    }

    printf("[P0][tick %d/%d] prioridad_pacman=%d prioridad_enemy=%d seleccionado=%s\n",
           global_tick,
           max_ticks,
           pacman_priority,
           enemy_priority,
           scheduler_process_name(selected));
    return PACMAN_OK;
}

const char *scheduler_process_name(SchedulerProcess process)
{
    if (process == SCHEDULER_PROCESS_PACMAN) {
        return "P1 Pac-Man";
    }

    return "P2 Enemy";
}
