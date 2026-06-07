.RECIPEPREFIX := >

CC=gcc

CFLAGS=-Wall -Wextra -Wpedantic -std=c11 -g -pthread -D_POSIX_C_SOURCE=200809L
LDFLAGS=-pthread -lrt

SRC=$(wildcard src/*.c)
OBJ=$(SRC:.c=.o)

TARGET=pacman

all: $(TARGET)

$(TARGET): $(OBJ)
>$(CC) $(CFLAGS) -Iinclude -o $(TARGET) $(OBJ) $(LDFLAGS)

clean:
>rm -f src/*.o $(TARGET)
>rm -rf build

run-caso1: all
>./$(TARGET) cases/Caso1 30

run-caso2: all
>./$(TARGET) cases/Caso2 30

run-caso3: all
>./$(TARGET) cases/Caso3 50

strace-caso1: all
>strace -f ./$(TARGET) cases/Caso1 30
