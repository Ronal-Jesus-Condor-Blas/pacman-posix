#ifndef PACMAN_SCHEDULER_H
#define PACMAN_SCHEDULER_H

#include "shared.h"

typedef enum {
    SCHEDULER_PROCESS_PACMAN = 0,
    SCHEDULER_PROCESS_ENEMY = 1
} SchedulerProcess;

typedef struct {
    shared_state_t *state;
    SchedulerProcess last_round_robin;
} Scheduler;

void scheduler_init(Scheduler *scheduler, shared_state_t *state);
int scheduler_apply_priority_requests(Scheduler *scheduler);
int scheduler_select_next_process(Scheduler *scheduler, SchedulerProcess *selected);
int scheduler_run_dry(Scheduler *scheduler);
int scheduler_print_tick_log(shared_state_t *state, SchedulerProcess selected);
const char *scheduler_process_name(SchedulerProcess process);

#endif
