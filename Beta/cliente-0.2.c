// cliente.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>      // Para fcntl
#include <sys/time.h>   // Para struct timeval
#include <errno.h>      // << CORRECCIÓN: Librería necesaria para errno y EINPROGRESS

#define MAX_BUFFER 1024
#define CONNECTION_TIMEOUT 5 // 5 segundos

// ... (La función mostrar_ayuda() no cambia)
void mostrar_ayuda() {
    printf("\n--- Comandos Admitidos ---\n");
    printf("CONSULTAS:\n");
    printf("  FIND|ID|<id_a_buscar>    - Busca un registro por su ID.\n");
    printf("  FIND|ALL                 - Muestra todos los registros.\n");
    printf("\nMODIFICACIONES (requieren transacción):\n");
    printf("  INSERT|<Nombre>|<Apellido>|<Localidad>|<Fecha>|<Numero>\n");
    printf("                           - Inserta un nuevo registro.\n");
    printf("  DELETE|<id_a_borrar>     - Elimina un registro por su ID.\n");
    printf("  UPDATE|<id>|<campo>|<nuevo_valor>\n");
    printf("                           - Modifica un campo de un registro.\n");
    printf("\nTRANSACCIONES:\n");
    printf("  BEGIN                    - Inicia una transacción y bloquea el archivo.\n");
    printf("  COMMIT                   - Confirma los cambios y libera el archivo.\n");
    printf("\nAPLICACIÓN:\n");
    printf("  HELP / ?                 - Muestra esta ayuda.\n");
    printf("  EXIT                     - Cierra la conexión y sale del programa.\n\n");
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
    
    // --- Lógica de Conexión con Timeout ---
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
                fprintf(stderr, "Error: Tiempo de conexión agotado. El servidor no responde en la IP/puerto especificado.\n");
                close(sock);
                return 1;
            } else {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);

                if (so_error != 0) {
                    fprintf(stderr, "Error al conectar con el servidor: %s\n", strerror(so_error));
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

    printf("¡Conectado exitosamente al servidor!\n");
    mostrar_ayuda();

    while (1) {
        printf("> ");
        fgets(message, MAX_BUFFER, stdin);
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

        int read_size = recv(sock, server_reply, MAX_BUFFER, 0);
        if (read_size > 0) {
            server_reply[read_size] = '\0';
            printf("Servidor: %s\n", server_reply);
        } else {
             printf("El servidor cerró la conexión.\n");
             break;
        }
    }

    close(sock);
    return 0;
}