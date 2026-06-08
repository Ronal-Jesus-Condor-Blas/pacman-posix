# Arquitectura del proyecto

El proyecto implementa una simulacion concurrente de Pac-Man en C usando procesos,
threads y mecanismos de sincronizacion POSIX. La arquitectura actual contiene tres
procesos: P0, P1 y P2. El renderer P3 no esta implementado.

## P0 - scheduler_process

P0 corresponde al proceso principal. Implementa el scheduler con threads internos
POSIX y mantiene la autoridad sobre ticks, seleccion de turnos, vidas y
finalizacion del juego.

Threads internos de P0:

- `tick_thread`: incrementa `global_tick`, controla `max_ticks` y solicita el fin
  del juego cuando se alcanza el limite de ticks.
- `scheduler_thread`: procesa solicitudes `SET_PRIORITY`, valida rangos, compara
  prioridades y aplica Round Robin cuando hay empate.
- `signal_thread`: entrega el turno al proceso seleccionado con `sem_post()` y
  espera la confirmacion con `sem_wait()`.
- `collision_manager_thread`: consume eventos de colision publicados por P2,
  descuenta vidas, limpia el evento y establece `game_over` si las vidas llegan a
  cero.

`collision_manager_thread` se mantiene separado para que P0 procese colisiones sin
mezclar esa responsabilidad con la senalizacion de turnos.

Responsabilidades:

- Leer argumentos de ejecucion: `./pacman <case_dir> <max_ticks>`.
- Leer y validar `map.txt`.
- Leer y validar archivos de movimientos.
- Crear memoria compartida POSIX con `shm_open()`.
- Ajustar tamano con `ftruncate()`.
- Mapear memoria con `mmap()`.
- Inicializar semaforos y mutexes POSIX.
- Crear P1 y P2 con `fork()`.
- Administrar `global_tick` y `max_ticks` desde `tick_thread`.
- Procesar solicitudes `SET_PRIORITY` desde `scheduler_thread`.
- Seleccionar el proceso que recibe turno segun prioridad.
- Resolver empates con Round Robin.
- Coordinar turnos con `sem_post()` y `sem_wait()`.
- Procesar eventos de colision publicados por P2 desde
  `collision_manager_thread`.
- Descontar `pacman_lives`.
- Establecer `game_over`.
- Desbloquear hijos al finalizar.
- Esperar P1 y P2 con `waitpid()`.
- Liberar recursos POSIX.

P0 es el unico responsable de modificar `pacman_lives` y de controlar
`game_over`, excepto por la inicializacion realizada al preparar la memoria
compartida.

## P1 - pacman_process

P1 se crea con `fork()` y ejecuta threads internos:

- `movement_reader_thread`
- `movement_executor_thread`
- `pacman_publisher_thread`

Responsabilidades:

- Leer `pacman_moves.txt`.
- Mantener una cola local de instrucciones protegida por mutex.
- Consumir como maximo una instruccion por turno autorizado.
- Validar movimientos contra limites del mapa y paredes `X`.
- Mantener posicion local de Pac-Man.
- Publicar `pacman_x`, `pacman_y` y `pacman_score` en memoria compartida.
- Solicitar cambios de prioridad escribiendo:
  - `pending_priority_pacman`
  - `priority_request_active`
- Leer `game_over` para finalizar ordenadamente.

P1 no modifica `pacman_lives` ni decide `game_over`.

## P2 - enemy_process

P2 se crea con `fork()` y ejecuta threads internos:

- `enemy_controller_thread`
- `ghost_thread_1`
- `ghost_thread_2`
- `ghost_thread_3`
- `ghost_thread_4`
- `pacman_tracker_thread`
- `collision_thread`

Responsabilidades:

- Leer `ghost_1_moves.txt` a `ghost_4_moves.txt`.
- Permitir que cada fantasma consuma como maximo una instruccion por turno de P2.
- Validar movimientos contra limites del mapa y paredes `X`.
- Mantener posiciones locales de los fantasmas.
- Leer la posicion publicada de Pac-Man.
- Detectar colisiones.
- Publicar eventos de colision:
  - `collision_detected`
  - `collision_tick`
  - `collision_ghost_id`
- Solicitar cambios de prioridad escribiendo:
  - `pending_priority_enemy`
  - `enemy_priority_request_active`
- Leer `game_over` para finalizar ordenadamente.

P2 no modifica `pacman_lives` ni decide `game_over`.

## Memoria compartida

La estructura `shared_state_t` contiene:

- `global_tick`
- `max_ticks`
- `game_over`
- `pacman_x`
- `pacman_y`
- `pacman_score`
- `pacman_lives`
- `ghost_x[]`
- `ghost_y[]`
- `collision_detected`
- `collision_tick`
- `collision_ghost_id`
- `prioridad_pacman`
- `prioridad_enemy`
- `pending_priority_pacman`
- `priority_request_active`
- `pending_priority_enemy`
- `enemy_priority_request_active`
- `map_rows`
- `map_cols`
- `map_grid`
- semaforos compartidos
- mutexes compartidos

La memoria compartida se administra con:

- `shm_open()`
- `ftruncate()`
- `mmap()`
- `munmap()`
- `shm_unlink()`

## Sincronizacion

Semaforos compartidos:

- `sem_pacman_turn`: P0 autoriza turno de P1.
- `sem_enemy_turn`: P0 autoriza turno de P2.
- `sem_p1_done`: P1 confirma fin de turno.
- `sem_p2_done`: P2 confirma fin de turno.

Mutexes compartidos:

- `state_mutex`: protege estado general.
- `priority_mutex`: protege prioridades y buzones de solicitud.
- `collision_mutex`: protege eventos de colision.

P1 y P2 tambien usan mutexes y semaforos internos no compartidos para coordinar sus
threads locales.

## Scheduler y prioridades

Prioridades por defecto:

- Pac-Man: 20.
- Enemy: 30.

Las prioridades pueden sobrescribirse con variables de entorno:

```bash
PACMAN_PRIORITY=40 ENEMY_PRIORITY=30 ./pacman cases/Caso1 10
```

Las solicitudes `SET_PRIORITY <numero>` no modifican prioridades directamente. P1
y P2 escriben solicitudes pendientes en memoria compartida, y P0 las valida y
aplica al inicio del siguiente tick. El rango valido es de 0 a 100.

Si las prioridades son iguales, P0 aplica desempate Round Robin alternando entre
P1 y P2.

## Colisiones y vidas

P2 solo publica eventos de colision. P0 consume esos eventos despues de cada turno
confirmado por P1 o P2. Para evitar procesar dos veces el mismo evento, P0 mantiene
`last_processed_collision_tick`.

Cuando P0 procesa una colision:

1. Lee `collision_tick` y `collision_ghost_id`.
2. Decrementa `pacman_lives`.
3. Limpia el evento de colision.
4. Si las vidas llegan a 0, establece `game_over=1`.

Pac-Man inicia con 3 vidas.
