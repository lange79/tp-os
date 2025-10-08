// cliente.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_BUFFER 1024

// --- Función para mostrar la ayuda ---
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
        printf("No se pudo crear el socket");
        return 1;
    }

    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error de conexión");
        return 1;
    }

    printf("Conectado al servidor.\n");
    
    // << MODIFICACIÓN: Mostrar ayuda al conectar >>
    mostrar_ayuda();

    while (1) {
        printf("> ");
        fgets(message, MAX_BUFFER, stdin);
        
        // Eliminar el salto de línea para una comparación limpia
        message[strcspn(message, "\r\n")] = 0;

        // << MODIFICACIÓN: Implementar comando HELP / ? >>
        if (strcmp(message, "HELP") == 0 || strcmp(message, "?") == 0) {
            mostrar_ayuda();
            continue; // Volver al prompt sin enviar el comando al servidor
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