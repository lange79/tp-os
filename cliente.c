// cliente.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/select.h>

#define MAX_BUFFER 2048
#define CONNECTION_TIMEOUT 5

void mostrar_ayuda() {
    printf("\n--- Comandos Admitidos (no distingue mayúsculas/minúsculas) ---\n");
    printf("CONSULTAS:\n");
    printf("  FIND|ALL                      - Muestra todos los registros.\n");
    printf("  FIND|<columna>|<valor>        - Busca un valor en una columna específica.\n");
    printf("    Columnas válidas: ID, Nombre, Apellido, Anio, Materia\n");
    printf("\nMODIFICACIONES (requieren transacción):\n");
    printf("  INSERT|<Nombre>|<Apellido>|<Anio>|<Materia>\n");
    printf("  DELETE|<ID>|<Nombre>|<Apellido>|<Anio>|<Materia>\n");
    printf("  UPDATE|<ID>|<columna>|<nuevo_valor>\n");
    printf("\nTRANSACCIONES:\n");
    printf("  BEGIN TRANSACTION             - Inicia una transacción y bloquea el archivo.\n");
    printf("  COMMIT TRANSACTION            - Confirma los cambios y libera el archivo.\n");
    printf("  ROLLBACK TRANSACTION          - Cancela la transacción y revierte los cambios.\n");
    printf("\nAPLICACIÓN:\n");
    printf("  HELP / ?                      - Muestra esta ayuda.\n");
    printf("  EXIT                          - Cierra la conexión y sale del programa.\n\n");
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <ip_servidor> <puerto>\n", argv[0]);
        return 1;
    }
    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int sock;
    struct sockaddr_in server_addr;
    char message[MAX_BUFFER], server_reply[MAX_BUFFER];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("No se pudo crear el socket");
        return 1;
    }
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    long arg = fcntl(sock, F_GETFL, NULL);
    arg |= O_NONBLOCK;
    fcntl(sock, F_SETFL, arg);

    int connect_res = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (connect_res < 0) {
        if (errno == EINPROGRESS) {
            printf("Conectando con el servidor (timeout %d segundos)...\n", CONNECTION_TIMEOUT);
            fd_set wait_set;
            struct timeval tv;
            tv.tv_sec = CONNECTION_TIMEOUT;
            tv.tv_usec = 0;
            FD_ZERO(&wait_set);
            FD_SET(sock, &wait_set);
            int select_res = select(sock + 1, NULL, &wait_set, NULL, &tv);
            if (select_res < 0) {
                perror("Error en select() durante la conexión");
                close(sock);
                return 1;
            } else if (select_res == 0) {
                fprintf(stderr, "Error: Tiempo de conexión agotado.\n");
                close(sock);
                return 1;
            } else {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error != 0) {
                    fprintf(stderr, "Error al conectar: %s\n", strerror(so_error));
                    close(sock);
                    return 1;
                }
            }
        } else {
            perror("Error de conexión");
            close(sock);
            return 1;
        }
    }
    
    arg = fcntl(sock, F_GETFL, NULL);
    arg &= (~O_NONBLOCK);
    fcntl(sock, F_SETFL, arg);

    printf("Conexión TCP establecida. Esperando confirmación del servidor...\n");

    char status_buffer[128];
    int read_size = recv(sock, status_buffer, sizeof(status_buffer) - 1, 0);

    if (read_size <= 0) {
        printf("Se perdió la conexión con el servidor, intente nuevamente.\n");
        close(sock);
        return 1;
    }
    status_buffer[read_size] = '\0';

    if (strcmp(status_buffer, "STATUS|QUEUED") == 0) {
        printf("Servidor ocupado. Has sido puesto en la cola de espera...\n");
        read_size = recv(sock, status_buffer, sizeof(status_buffer) - 1, 0);
        if (read_size <= 0) {
            printf("Se perdió la conexión con el servidor mientras esperabas.\n");
            close(sock);
            return 1;
        }
        status_buffer[read_size] = '\0';
    }
    if (strcmp(status_buffer, "STATUS|CONNECTED") != 0) {
        printf("Respuesta inesperada del servidor: %s\n", status_buffer);
        close(sock);
        return 1;
    }

    printf("\n¡Conexión activa establecida con el servidor!\n");
    mostrar_ayuda();
    
    fd_set read_fds;
    while (1) {
        printf("> ");
        fflush(stdout);

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        if (select(sock + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            int reply_size = recv(sock, server_reply, MAX_BUFFER - 1, 0);
            if (reply_size > 0) {
                server_reply[reply_size] = '\0';
                printf("\rServidor: %s\n> ", server_reply);
                fflush(stdout);
            } else {
                 printf("\rSe perdió la conexión con el servidor, intente nuevamente.\n");
                 break;
            }
        }
        
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(message, MAX_BUFFER, stdin) == NULL) break;
            
            message[strcspn(message, "\r\n")] = 0;
            if (strcmp(message, "HELP") == 0 || strcmp(message, "?") == 0) {
                mostrar_ayuda();
                continue;
            }
            if (send(sock, message, strlen(message), 0) < 0) {
                puts("Send failed");
                break;
            }
            if (strcmp(message, "EXIT") == 0) {
                printf("Desconectando...\n");
                break;
            }
        }
    }
    close(sock);
    return 0;
}