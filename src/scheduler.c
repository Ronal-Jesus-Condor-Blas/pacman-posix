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

    printf("[tick %d/%d] prioridad_pacman=%d prioridad_enemy=%d seleccionado=%s\n",
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
