#include "../include/processes.h"
#include "../include/pacman.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

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

static int wait_for_turn(sem_t *turn_sem)
{
    while (sem_wait(turn_sem) != 0) {
        if (errno != EINTR) {
            perror("sem_wait");
            return PACMAN_ERROR;
        }
    }

    return PACMAN_OK;
}

static int finish_turn(sem_t *done_sem)
{
    if (sem_post(done_sem) != 0) {
        perror("sem_post");
        return PACMAN_ERROR;
    }

    return PACMAN_OK;
}

static int child_loop(shared_state_t *state,
                      sem_t *turn_sem,
                      sem_t *done_sem,
                      const char *prefix,
                      const char *process_name)
{
    printf("[%s] PID=%ld inicio como %s\n", prefix, (long)getpid(), process_name);
    fflush(stdout);

    while (1) {
        int game_over = 0;
        int tick = 0;

        if (wait_for_turn(turn_sem) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (read_game_over(state, &game_over) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        if (game_over) {
            break;
        }

        if (read_global_tick(state, &tick) != PACMAN_OK) {
            return PACMAN_ERROR;
        }

        printf("[%s] Turno recibido en tick %d\n", prefix, tick);
        fflush(stdout);

        if (finish_turn(done_sem) != PACMAN_OK) {
            return PACMAN_ERROR;
        }
    }

    printf("[%s] Finalizando %s\n", prefix, process_name);
    fflush(stdout);
    return PACMAN_OK;
}

static void run_pacman_process(shared_state_t *state, const char *case_dir)
{
    int status = pacman_process_run(state, case_dir);
    _exit(status == PACMAN_OK ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void run_enemy_process(shared_state_t *state)
{
    int status = child_loop(state,
                            &state->sem_enemy_turn,
                            &state->sem_p2_done,
                            "P2",
                            "enemy_process");
    _exit(status == PACMAN_OK ? EXIT_SUCCESS : EXIT_FAILURE);
}

static int wait_one_child(pid_t pid, const char *label)
{
    int status = 0;
    pid_t result;

    if (pid <= 0) {
        return PACMAN_OK;
    }

    do {
        result = waitpid(pid, &status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("waitpid");
        return PACMAN_ERROR;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
        printf("[P0] %s finalizo correctamente\n", label);
        return PACMAN_OK;
    }

    if (WIFEXITED(status)) {
        fprintf(stderr, "[P0] %s finalizo con codigo %d\n", label, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "[P0] %s termino por senal %d\n", label, WTERMSIG(status));
    } else {
        fprintf(stderr, "[P0] %s finalizo con estado inesperado\n", label);
    }

    return PACMAN_ERROR;
}

int processes_start(shared_state_t *state, const char *case_dir, ProcessHandles *handles)
{
    pid_t pid;

    handles->pacman_pid = -1;
    handles->enemy_pid = -1;

    printf("[P0] Creando P1 pacman_process...\n");
    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return PACMAN_ERROR;
    }
    if (pid == 0) {
        run_pacman_process(state, case_dir);
    }
    handles->pacman_pid = pid;

    printf("[P0] Creando P2 enemy_process...\n");
    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return PACMAN_ERROR;
    }
    if (pid == 0) {
        run_enemy_process(state);
    }
    handles->enemy_pid = pid;

    return PACMAN_OK;
}

int processes_wait(ProcessHandles *handles)
{
    int status = PACMAN_OK;

    printf("[P0] esperando hijos con waitpid()...\n");

    if (wait_one_child(handles->pacman_pid, "P1 pacman_process") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }

    if (wait_one_child(handles->enemy_pid, "P2 enemy_process") != PACMAN_OK) {
        status = PACMAN_ERROR;
    }

    return status;
}
