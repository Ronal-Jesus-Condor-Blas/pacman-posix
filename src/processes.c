#include "../include/processes.h"
#include "../include/enemy.h"
#include "../include/pacman.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void run_pacman_process(shared_state_t *state, const char *case_dir)
{
    int status = pacman_process_run(state, case_dir);
    _exit(status == PACMAN_OK ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void run_enemy_process(shared_state_t *state, const char *case_dir)
{
    int status = enemy_process_run(state, case_dir);
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
        run_enemy_process(state, case_dir);
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
