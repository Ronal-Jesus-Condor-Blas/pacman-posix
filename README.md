# Pac-Man Concurrente en C con POSIX

## Descripcion del proyecto

Este proyecto implementa una simulacion concurrente de Pac-Man en lenguaje C usando
APIs POSIX sobre Linux/WSL Ubuntu. El programa lee un caso de prueba compuesto por
un mapa y archivos de movimientos, inicializa memoria compartida, crea procesos con
`fork()`, coordina turnos con semaforos y ejecuta logica concurrente mediante
threads internos en los procesos de Pac-Man y enemigos.

La version actual no implementa interfaz grafica, `ncurses` ni el proceso renderer
P3. P3 se mantiene como funcionalidad opcional/bonus no implementada.

## Arquitectura general

La arquitectura usa tres procesos principales:

- `P0` o `scheduler_process`: proceso principal del programa.
- `P1` o `pacman_process`: proceso hijo responsable de Pac-Man.
- `P2` o `enemy_process`: proceso hijo responsable de los fantasmas.

P0 centraliza el scheduler en su proceso principal. No existen `tick_thread`,
`scheduler_thread` ni `signal_thread` en P0. El scheduler avanza por ticks, decide
que proceso recibe turno, procesa cambios de prioridad solicitados por buzon,
consume eventos de colision publicados por P2, descuenta vidas, decide `game_over`
y espera a los hijos con `waitpid()`.

## Procesos e hilos

### P0 - scheduler_process

Responsabilidades actuales:

- Leer y validar el caso de entrada.
- Crear memoria compartida POSIX.
- Inicializar semaforos y mutexes POSIX.
- Crear P1 y P2 usando `fork()`.
- Administrar `global_tick`, `max_ticks`, prioridades y Round Robin.
- Otorgar turnos con `sem_post()`.
- Esperar fin de turno con `sem_wait()`.
- Procesar `collision_detected`, `collision_tick` y `collision_ghost_id`.
- Descontar `pacman_lives`.
- Establecer `game_over`.
- Desbloquear procesos hijos al finalizar.
- Esperar hijos con `waitpid()`.
- Liberar memoria compartida con `munmap()`, `close()` y `shm_unlink()`.

### P1 - pacman_process

P1 usa tres threads internos POSIX:

- `movement_reader_thread`
- `movement_executor_thread`
- `pacman_publisher_thread`

P1 lee `pacman_moves.txt`, mantiene una cola local protegida por mutex, consume
como maximo una instruccion por turno autorizado, valida movimientos contra mapa y
paredes, publica `pacman_x`, `pacman_y` y `pacman_score` en memoria compartida, y
solicita cambios de prioridad mediante `pending_priority_pacman` y
`priority_request_active`.

P1 no modifica `pacman_lives` ni controla `game_over`; solo lee `game_over` para
terminar ordenadamente.

### P2 - enemy_process

P2 usa siete threads internos POSIX:

- `enemy_controller_thread`
- `ghost_thread_1`
- `ghost_thread_2`
- `ghost_thread_3`
- `ghost_thread_4`
- `pacman_tracker_thread`
- `collision_thread`

Cada `ghost_thread_N` lee su propio archivo `ghost_N_moves.txt`. El controlador de
P2 espera `sem_enemy_turn`, permite que cada fantasma consuma como maximo una
instruccion por turno, espera a los cuatro fantasmas, actualiza la copia local de
la posicion de Pac-Man y activa la deteccion de colisiones.

P2 no modifica `pacman_lives` ni `game_over`. Solo publica eventos de colision:

- `collision_detected`
- `collision_tick`
- `collision_ghost_id`

## Memoria compartida

La memoria compartida se implementa con:

- `shm_open()`
- `ftruncate()`
- `mmap()`
- `munmap()`
- `shm_unlink()`

La estructura compartida contiene, entre otros campos:

- `global_tick`
- `max_ticks`
- `game_over`
- `pacman_x`
- `pacman_y`
- `pacman_score`
- `pacman_lives`
- posiciones iniciales y estado de fantasmas
- evento de colision
- prioridades actuales y pendientes
- `map_grid`
- semaforos de turno y confirmacion
- mutexes de estado, prioridad y colision

Pac-Man inicia con `PACMAN_INITIAL_LIVES = 3`.

## Sincronizacion

Se usan semaforos POSIX compartidos entre procesos:

- `sem_pacman_turn`
- `sem_enemy_turn`
- `sem_p1_done`
- `sem_p2_done`

P0 publica un turno al proceso seleccionado y espera la confirmacion de fin de
turno. P1 y P2 usan semaforos y mutexes internos para coordinar sus propios
threads.

Se usan mutexes POSIX process-shared para proteger:

- estado general (`state_mutex`)
- prioridades (`priority_mutex`)
- eventos de colision (`collision_mutex`)

## Scheduler y prioridades

Prioridades por defecto:

- Pac-Man: `DEFAULT_PACMAN_PRIORITY = 20`
- Enemy: `DEFAULT_ENEMY_PRIORITY = 30`

Pueden sobrescribirse mediante variables de entorno:

```bash
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso1 10
```

Los valores se validan dentro del rango:

- `MIN_PRIORITY = 0`
- `MAX_PRIORITY = 100`

`SET_PRIORITY <numero>` no modifica directamente la prioridad activa. P1 o P2
escriben una solicitud pendiente en memoria compartida y P0 la aplica al inicio del
siguiente tick. Si ambas prioridades son iguales, el scheduler aplica desempate
Round Robin alternando entre P1 y P2.

## Archivos de entrada

Cada caso debe contener:

- `map.txt`
- `pacman_moves.txt`
- `ghost_1_moves.txt`
- `ghost_2_moves.txt`
- `ghost_3_moves.txt`
- `ghost_4_moves.txt`

El mapa usa la siguiente codificacion:

- `X`: pared
- `O`: camino libre
- `P`: posicion inicial de Pac-Man
- `A`: fantasma 1
- `B`: fantasma 2
- `C`: fantasma 3
- `D`: fantasma 4

Instrucciones permitidas:

- `UP`
- `DOWN`
- `LEFT`
- `RIGHT`
- `SET_PRIORITY <numero>`

Un movimiento hacia `X` o fuera de los limites es invalido. El personaje permanece
en su posicion y el turno se consume.

## Compilacion

```bash
make clean
make
```

Resultado observado: compilacion limpia con `-Wall -Wextra -Wpedantic`, sin
warnings relevantes.

## Ejecucion

Uso general:

```bash
./pacman <case_dir> <max_ticks>
```

Ejemplos:

```bash
./pacman cases/Caso1 10
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso3 10
```

## Pruebas realizadas

Compilacion:

```bash
make clean
make
```

Casos base:

```bash
./pacman cases/Caso1 10
./pacman cases/Caso2 10
./pacman cases/Caso3 10
```

Prioridad inicial de Pac-Man:

```bash
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso1 10
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso2 10
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso3 10
```

Prioridad inicial de enemigos:

```bash
PACMAN_PRIORITY=20 ENEMY_PRIORITY=40 ./pacman cases/Caso1 10
```

## Resultados esperados y observados

### Caso1 normal

- Enemy tiene prioridad 30 sobre Pac-Man 20.
- P2 recibe turnos.
- Se detectan colisiones en ticks 2, 4 y 6.
- P0 descuenta vidas: 3 -> 2 -> 1 -> 0.
- P0 finaliza por vidas agotadas.
- Resumen final observado: `ticks ejecutados=6`, `lives=0`, `game_over=1`.

### Caso2 normal

- P2 recibe turnos.
- Se detecta una colision en tick 2.
- Las vidas quedan en 2.
- Termina por `max_ticks=10`.
- Resumen final observado: `ticks ejecutados=10`, `lives=2`, `game_over=1`.

### Caso3 normal

- P2 solicita `SET_PRIORITY enemy=10`.
- P0 aplica la prioridad al siguiente tick.
- P1 solicita `SET_PRIORITY=15` y luego `SET_PRIORITY=5`.
- P0 aplica los cambios al siguiente tick.
- El scheduler cambia entre P1 y P2 segun prioridad.
- Termina por `max_ticks=10`.

### Con PACMAN_PRIORITY=40 ENEMY_PRIORITY=30

- P1 recibe turnos inicialmente.
- Pac-Man consume instrucciones, se mueve y publica estado.
- Cuando aparece `SET_PRIORITY`, P0 aplica el cambio al siguiente tick y puede
  cambiar el proceso elegido.

### Con PACMAN_PRIORITY=20 ENEMY_PRIORITY=40

- P2 recibe turnos.
- En Caso1, el juego termina por vidas agotadas en tick 6.

## Validaciones tecnicas

Verificacion de escrituras sobre vidas y fin de juego:

```bash
grep -R --exclude='*.o' "state->pacman_lives[[:space:]]*=" src include
grep -R --exclude='*.o' "state->game_over[[:space:]]*=" src include
```

Interpretacion:

- `pacman_lives` se inicializa en `src/shared.c`.
- `pacman_lives` se administra en `src/scheduler.c`.
- `game_over` se inicializa en `src/shared.c`.
- `game_over` se escribe desde `src/scheduler.c`.
- `src/pacman.c` y `src/enemy.c` solo leen `game_over` para finalizar
  ordenadamente.
- P2 no escribe `pacman_lives` ni controla `game_over`.

## Limitaciones actuales

- No se implementa renderer grafico P3.
- No se usa `ncurses`.
- No hay interfaz visual interactiva.
- La salida se presenta mediante logs en consola.
- P0 centraliza el scheduler en el flujo principal del proceso, sin threads
  internos propios.

## Conclusion

El proyecto implementa una simulacion concurrente funcional basada en procesos,
threads, memoria compartida, semaforos y mutexes POSIX. La responsabilidad de
planificacion, prioridades, vidas y finalizacion del juego queda centralizada en
P0, mientras que P1 y P2 ejecutan sus responsabilidades concurrentes internas y
publican estado o eventos en memoria compartida.

## Autores / curso

- Autor(es): [completar]
- Curso: [completar]
- Universidad/Institucion: [completar]
