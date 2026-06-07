# Arquitectura del proyecto

## P0 - scheduler_process

Responsabilidades:
- Inicializar memoria compartida.
- Leer y validar map.txt.
- Inicializar mutex y semáforos POSIX.
- Crear P1 y P2 con fork().
- Controlar global_tick.
- Decidir qué proceso ejecuta según prioridades.
- Resolver empates con Round Robin.
- Procesar solicitudes SET_PRIORITY.
- Procesar eventos de colisión.
- Actualizar vidas y game_over.

## P1 - pacman_process

Hilos recomendados:
- movement_reader_thread.
- movement_executor_thread.
- pacman_publisher_thread.

Responsabilidades:
- Leer pacman_moves.txt.
- Mover a Pac-Man cuando P0 le da turno.
- Validar movimientos contra map_grid.
- Publicar posición y score en memoria compartida.
- Solicitar cambios de prioridad mediante buzón.

## P2 - enemy_process

Hilos recomendados:
- enemy_controller_thread.
- ghost_thread_1.
- ghost_thread_2.
- ghost_thread_3.
- ghost_thread_4.
- pacman_tracker_thread.
- collision_thread.

Responsabilidades:
- Leer movimientos de ghost_1_moves.txt a ghost_4_moves.txt.
- Mover fantasmas cuando P0 da turno a P2.
- Mantener posiciones internas de fantasmas.
- Leer posición de Pac-Man desde memoria compartida.
- Detectar colisiones.
- Publicar eventos de colisión.
- Solicitar cambios de prioridad mediante buzón.

## Memoria compartida

Debe contener:
- global_tick.
- max_ticks.
- game_over.
- pacman_x.
- pacman_y.
- pacman_score.
- pacman_lives.
- collision_detected.
- collision_tick.
- collision_ghost_id.
- prioridad_pacman.
- prioridad_enemy.
- pending_priority_pacman.
- priority_request_active.
- pending_priority_enemy.
- enemy_priority_request_active.
- map_grid.

## Sincronización

Usar:
- sem_pacman_turn.
- sem_enemy_turn.
- sem_p1_done.
- sem_p2_done.
- pthread_mutex_t con PTHREAD_PROCESS_SHARED para memoria compartida.
