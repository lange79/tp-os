# Makefile para el TP de Sistemas Operativos
CC = gcc
CFLAGS = -g -Wall -pthread
SERVER_EXEC = servidor
CLIENT_EXEC = cliente
SERVER_SRC = servidor.c
CLIENT_SRC = cliente.c

all: $(SERVER_EXEC) $(CLIENT_EXEC)

$(SERVER_EXEC): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_EXEC) $(SERVER_SRC)

$(CLIENT_EXEC): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_EXEC) $(CLIENT_SRC)

clean:
	rm -f $(SERVER_EXEC) $(CLIENT_EXEC) *.o