#ifndef PACMAN_PROCESSES_H
#define PACMAN_PROCESSES_H

#include <sys/types.h>

#include "shared.h"

typedef struct {
    pid_t pacman_pid;
    pid_t enemy_pid;
} ProcessHandles;

int processes_start(shared_state_t *state, ProcessHandles *handles);
int processes_wait(ProcessHandles *handles);

#endif
