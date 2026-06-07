# Contexto para Codex

Proyecto: Pac-Man Concurrente en C con POSIX.

Entorno:
- Ubuntu en WSL desde VS Code.
- Compilador: gcc.
- Build: Makefile.
- Lenguaje: C11.
- Usar APIs POSIX.

Objetivo:
Implementar una simulación concurrente de Pac-Man usando procesos, threads, memoria compartida, semáforos y mutex.

Arquitectura obligatoria:
- P0: scheduler_process.
- P1: pacman_process.
- P2: enemy_process.
- P3 renderer_process es opcional y no debe implementarse al inicio.

Reglas principales:
- P0 inicializa memoria compartida, semáforos, mutex, mapa, ticks y prioridades.
- P0 crea P1 y P2 con fork().
- P0 decide qué proceso ejecuta en cada tick.
- P1 mueve a Pac-Man leyendo pacman_moves.txt.
- P2 mueve fantasmas leyendo ghost_1_moves.txt a ghost_4_moves.txt.
- P2 no resta vidas. Solo publica eventos de colisión.
- P0 procesa colisiones y actualiza pacman_lives y game_over.
- SET_PRIORITY no debe modificar prioridades directamente.
- P1/P2 deben solicitar cambio de prioridad mediante buzón en memoria compartida.
- P0 valida y aplica el cambio al inicio del siguiente tick.
- Cada proceso consume máximo una acción por turno autorizado.
- Un movimiento inválido contra pared X consume turno.
- El proyecto debe priorizar sincronización correcta sobre gráficos.

POSIX requerido:
- fork()
- waitpid()
- pthread_create()
- pthread_join()
- pthread_mutex_t
- sem_t
- shm_open()
- mmap()
- munmap()
- shm_unlink()

Compilación:
make clean
make

Ejecución:
./pacman cases/Caso1 30
./pacman cases/Caso2 30
./pacman cases/Caso3 50

No implementar todavía:
- Renderer gráfico.
- ncurses.
- interfaz visual compleja.
- lógica innecesaria fuera del enunciado.
