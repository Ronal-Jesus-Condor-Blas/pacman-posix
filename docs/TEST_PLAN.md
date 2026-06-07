# Plan de pruebas

## Caso 1

Objetivo:
- Validar lectura de mapa.
- Validar movimientos repetitivos de Pac-Man.
- Validar paredes.
- Validar colisiones básicas.

Comando:
./pacman cases/Caso1 30

## Caso 2

Objetivo:
- Validar colisión directa entre Pac-Man y fantasmas.
- Validar que P2 publique evento de colisión.
- Validar que P0 reste vidas.

Comando:
./pacman cases/Caso2 30

## Caso 3

Objetivo:
- Validar SET_PRIORITY.
- Validar buzones de solicitud de prioridad.
- Validar que P0 aplique prioridades.
- Validar desempate Round Robin.

Comando:
./pacman cases/Caso3 50

## Validaciones técnicas

Compilar:
make clean
make

Ejecutar con strace:
strace -f ./pacman cases/Caso1 30

Ejecutar con valgrind:
valgrind --leak-check=full ./pacman cases/Caso1 30
