# Plan de pruebas

Este documento resume las pruebas aplicables al estado actual del proyecto
Pac-Man Concurrente en C con POSIX.

## Compilacion

Comandos:

```bash
make clean
make
```

Resultado esperado:

- Compilacion limpia.
- Sin warnings relevantes con `-Wall -Wextra -Wpedantic`.
- Generacion del ejecutable `pacman`.

## Ejecucion general

Formato:

```bash
./pacman <case_dir> <max_ticks>
```

Ejemplo:

```bash
./pacman cases/Caso1 10
```

## Casos base

```bash
./pacman cases/Caso1 10
./pacman cases/Caso2 10
./pacman cases/Caso3 10
```

### Caso1

Objetivos:

- Validar lectura de mapa.
- Validar movimiento de fantasmas.
- Validar colisiones basicas.
- Validar decremento de vidas en P0.
- Validar finalizacion por vidas agotadas.

Resultado observado:

- Enemy tiene prioridad 30 sobre Pac-Man 20.
- P2 recibe turnos.
- Se detectan colisiones en ticks 2, 4 y 6.
- P0 descuenta vidas: 3 -> 2 -> 1 -> 0.
- P0 finaliza por vidas agotadas.
- Resumen final: `ticks ejecutados=6`, `lives=0`, `game_over=1`.

### Caso2

Objetivos:

- Validar colision directa entre Pac-Man y fantasmas.
- Validar publicacion de colision por P2.
- Validar procesamiento de colision por P0.
- Validar finalizacion por `max_ticks`.

Resultado observado:

- P2 recibe turnos.
- Se detecta una colision en tick 2.
- Las vidas quedan en 2.
- Termina por `max_ticks=10`.
- Resumen final: `ticks ejecutados=10`, `lives=2`, `game_over=1`.

### Caso3

Objetivos:

- Validar `SET_PRIORITY`.
- Validar buzones de solicitud de prioridad.
- Validar aplicacion de prioridades por P0 al siguiente tick.
- Validar cambios de seleccion del scheduler.
- Validar Round Robin si hay empate de prioridades.

Resultado observado:

- P2 solicita `SET_PRIORITY enemy=10`.
- P0 aplica la prioridad al siguiente tick.
- P1 solicita `SET_PRIORITY=15` y luego `SET_PRIORITY=5`.
- P0 aplica los cambios al siguiente tick.
- El scheduler cambia de P2 a P1 o viceversa segun prioridad.
- Termina por `max_ticks=10`.

## Pruebas con prioridad inicial de Pac-Man

```bash
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso1 10
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso2 10
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso3 10
```

Resultado esperado:

- P1 recibe turnos inicialmente.
- Pac-Man consume instrucciones.
- Pac-Man valida movimientos contra paredes.
- Pac-Man publica `pacman_x`, `pacman_y` y `pacman_score`.
- Si aparece `SET_PRIORITY`, P0 aplica el cambio al siguiente tick.

## Prueba con prioridad inicial de enemigos

```bash
PACMAN_PRIORITY=20 ENEMY_PRIORITY=40 ./pacman cases/Caso1 10
```

Resultado observado:

- P2 recibe turnos.
- Se detectan colisiones.
- Caso1 termina por vidas agotadas en tick 6.

## Validaciones tecnicas con grep

Verificar creacion de threads POSIX:

```bash
grep -R --exclude='*.o' "pthread_create" src include
```

Interpretacion:

- Deben aparecer llamadas en P0 (`src/scheduler.c`), P1 (`src/pacman.c`) y P2
  (`src/enemy.c`).
- P0 debe crear `tick_thread`, `scheduler_thread`, `signal_thread` y
  `collision_manager_thread`.

Verificar threads obligatorios de P0:

```bash
grep -R --exclude='*.o' "tick_thread\|scheduler_thread\|signal_thread" src include
grep -R --exclude='*.o' "collision_manager_thread" src include
```

Interpretacion:

- Los tres threads obligatorios de P0 deben encontrarse en `src/scheduler.c`.
- `collision_manager_thread` debe encontrarse en `src/scheduler.c` como thread
  adicional justificado para el consumo de colisiones en P0.

Verificar escrituras sobre vidas:

```bash
grep -R --exclude='*.o' "state->pacman_lives[[:space:]]*=" src include
```

Interpretacion:

- `pacman_lives` se inicializa en `src/shared.c`.
- `pacman_lives` se administra desde `src/scheduler.c`.
- P1 y P2 no controlan vidas.

Verificar escrituras sobre fin de juego:

```bash
grep -R --exclude='*.o' "state->game_over[[:space:]]*=" src include
```

Interpretacion:

- `game_over` se inicializa en `src/shared.c`.
- `game_over` se escribe desde `src/scheduler.c`.
- P1 y P2 leen `game_over` para finalizar ordenadamente.
- P2 no controla el fin del juego.

## Validaciones opcionales

Estas pruebas son utiles para depuracion adicional:

```bash
strace -f ./pacman cases/Caso1 10
valgrind --leak-check=full ./pacman cases/Caso1 10
```

## Criterios de aprobacion

- El proyecto compila con `make`.
- El programa acepta `./pacman <case_dir> <max_ticks>`.
- P0 crea P1 y P2 con `fork()`.
- P1 y P2 finalizan sin procesos zombies.
- P0 usa `tick_thread`, `scheduler_thread` y `signal_thread`.
- P0 usa `collision_manager_thread` para procesar colisiones.
- P1 usa sus tres threads internos.
- P2 usa sus siete threads internos.
- P0 procesa colisiones y descuenta vidas.
- P0 establece `game_over` por vidas agotadas o por `max_ticks`.
- No se implementa renderer P3.
- No se usa `ncurses`.
