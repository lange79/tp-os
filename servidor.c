// servidor.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <limits.h>
#include <strings.h> // Para strcasecmp

#define MAX_BUFFER 2048
#define CSV_FILE "datos.csv"
#define MAX_COLS 10

// --- Variables globales para el estado del servidor ---
int active_clients_count = 0;
int waiting_clients_count = 0;
int max_concurrent_clients_config;
int max_waiting_clients_config;
volatile int shutdown_flag = 0;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

// --- Variables globales para la estructura del CSV ---
char *column_names[MAX_COLS];
int column_count = 0;
char csv_header[1024];

typedef struct {
    int client_socket;
    struct sockaddr_in client_address;
} thread_args_t;

int file_fd; // Descriptor de archivo para el bloqueo fcntl
pthread_mutex_t file_lock_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para el bloqueo fcntl

void print_server_status() {
    printf("\n\n================ STATUS ==============\n");
    printf("Clientes conectados : %d/%d\n", active_clients_count, max_concurrent_clients_config);
    printf("Clientes en cola    : %d/%d\n", waiting_clients_count, max_waiting_clients_config);
    printf("======================================\n\n");
}

void load_csv_header() {
    FILE *fs = fopen(CSV_FILE, "r");
    if (fs == NULL) return;

    if (fgets(csv_header, sizeof(csv_header), fs)) {
        csv_header[strcspn(csv_header, "\r\n")] = 0;
        char header_copy[1024];
        strcpy(header_copy, csv_header);
        char *token, *saveptr;
        token = strtok_r(header_copy, ";", &saveptr);
        while (token != NULL && column_count < MAX_COLS) {
            column_names[column_count++] = strdup(token);
            token = strtok_r(NULL, ";", &saveptr);
        }
    }
    fclose(fs);
    printf(">> Cabeceras del CSV cargadas: %d columnas.\n", column_count);
}

int get_column_index(const char *col_name) {
    for (int i = 0; i < column_count; i++) {
        if (strcasecmp(column_names[i], col_name) == 0) return i;
    }
    return -1;
}

int find_first_free_id(const char* filename) {
    FILE *fs = fopen(filename, "r");
    if (!fs) return 1;
    char line[1024];
    char seen_ids[USHRT_MAX] = {0};
    fgets(line, sizeof(line), fs); // Skip header
    while (fgets(line, sizeof(line), fs)) {
        int id = atoi(line);
        if (id > 0 && id < USHRT_MAX) seen_ids[id] = 1;
    }
    fclose(fs);
    for (int i = 1; i < USHRT_MAX; i++) {
        if (seen_ids[i] == 0) return i;
    }
    return USHRT_MAX;
}

void *handle_client(void *args) {
    thread_args_t *thread_args = (thread_args_t *)args;
    int client_socket = thread_args->client_socket;
    char buffer[MAX_BUFFER];
    int in_transaction = 0;
    char temp_file_name[256];
    char current_data_source[256];

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(thread_args->client_address.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(thread_args->client_address.sin_port);

    pthread_mutex_lock(&count_mutex);
    if (waiting_clients_count >= max_waiting_clients_config) {
        printf(">> Conexión de %s:%d rechazada: Cola de espera llena.\n", client_ip, client_port);
        send(client_socket, "ERROR|Cola de espera llena. Intente más tarde.", 46, 0);
        pthread_mutex_unlock(&count_mutex);
        close(client_socket);
        free(args);
        return NULL;
    }
    
    while (active_clients_count >= max_concurrent_clients_config && !shutdown_flag) {
        send(client_socket, "STATUS|QUEUED", 13, 0);
        waiting_clients_count++;
        printf(">> Cliente %s:%d puesto en cola de espera.\n", client_ip, client_port);
        print_server_status();
        pthread_cond_wait(&queue_cond, &count_mutex);
        waiting_clients_count--;
    }
    if (shutdown_flag) {
        send(client_socket, "ERROR|Servidor en apagado.", 26, 0);
        pthread_mutex_unlock(&count_mutex);
        close(client_socket);
        free(args);
        return NULL;
    }
    active_clients_count++;
    send(client_socket, "STATUS|CONNECTED", 16, 0);
    printf(">> Cliente %s:%d activado.\n", client_ip, client_port);
    print_server_status();
    pthread_mutex_unlock(&count_mutex);

    int read_size;
    while (!shutdown_flag && (read_size = recv(client_socket, buffer, MAX_BUFFER, 0)) > 0) {
        buffer[read_size] = '\0';
        buffer[strcspn(buffer, "\r\n")] = 0;
        printf("Recibido de %s:%d: %s\n", client_ip, client_port, buffer);
        char response[MAX_BUFFER] = "ERROR|Comando desconocido";
        char temp_buffer[MAX_BUFFER];
        strcpy(temp_buffer, buffer);
        char *saveptr1;

        if (strcmp(temp_buffer, "BEGIN TRANSACTION") == 0) {
            if (in_transaction) { strcpy(response, "ERROR|Ya hay una transacción activa."); }
            else {
                if (pthread_mutex_trylock(&file_lock_mutex) != 0) {
                    strcpy(response, "ERROR|El archivo está bloqueado por otra transacción. Intente más tarde.");
                } else {
                    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
                    if (fcntl(file_fd, F_SETLK, &lock) == -1) {
                        strcpy(response, "ERROR|Fallo al bloquear el archivo.");
                        pthread_mutex_unlock(&file_lock_mutex);
                    } else {
                        sprintf(temp_file_name, "datos_%ld.tmp", (long)pthread_self());
                        strcpy(current_data_source, CSV_FILE);
                        in_transaction = 1;
                        strcpy(response, "OK|Transacción iniciada.");
                    }
                }
            }
        } else if (strcmp(temp_buffer, "COMMIT TRANSACTION") == 0) {
            if (!in_transaction) { strcpy(response, "ERROR|No hay una transacción activa."); }
            else {
                if (access(temp_file_name, F_OK) == 0) {
                    if (rename(temp_file_name, CSV_FILE) == 0) strcpy(response, "OK|Transacción confirmada. Cambios guardados.");
                    else { strcpy(response, "ERROR|Fallo crítico al guardar los cambios."); remove(temp_file_name); }
                } else { strcpy(response, "OK|Transacción confirmada. No se realizaron cambios."); }
                in_transaction = 0;
                struct flock lock = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
                fcntl(file_fd, F_SETLK, &lock);
                pthread_mutex_unlock(&file_lock_mutex);
            }
        } else if (strcmp(temp_buffer, "ROLLBACK TRANSACTION") == 0) {
            if (!in_transaction) { strcpy(response, "ERROR|No hay una transacción activa para revertir."); }
            else {
                remove(temp_file_name);
                in_transaction = 0;
                struct flock lock = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
                fcntl(file_fd, F_SETLK, &lock);
                pthread_mutex_unlock(&file_lock_mutex);
                strcpy(response, "OK|Transacción revertida. Se descartaron todos los cambios.");
            }
        } else if (strncmp(temp_buffer, "INSERT", 6) == 0 || strncmp(temp_buffer, "DELETE", 6) == 0 || strncmp(temp_buffer, "UPDATE", 6) == 0) {
            if (!in_transaction) { strcpy(response, "ERROR|Esta operación requiere una transacción."); }
            else {
                char *command = strtok_r(temp_buffer, "|", &saveptr1);
                char next_temp_file[256];
                
                int len = snprintf(next_temp_file, sizeof(next_temp_file), "%s_next", temp_file_name);
                if (len >= sizeof(next_temp_file)) {
                    strcpy(response, "ERROR|Nombre de archivo temporal demasiado largo. Abortando transacción.");
                } else {
                    FILE *source = fopen(current_data_source, "r");
                    FILE *dest = fopen(next_temp_file, "w");
                    
                    if (!source || !dest) { strcpy(response, "ERROR|No se pudieron abrir los archivos de la transacción."); }
                    else {
                        if (strcasecmp(command, "INSERT") == 0) {
                            char *nombre = strtok_r(NULL, "|", &saveptr1); char *apellido = strtok_r(NULL, "|", &saveptr1);
                            char *anio = strtok_r(NULL, "|", &saveptr1); char *materia = strtok_r(NULL, "|", &saveptr1);
                            if (!nombre || !apellido || !anio || !materia) { strcpy(response, "ERROR|Sintaxis: INSERT|<Nombre>|<Apellido>|<Anio>|<Materia>"); }
                            else {
                                int new_id = find_first_free_id(current_data_source);
                                char line[1024];
                                while(fgets(line, sizeof(line), source)) fputs(line, dest);
                                fprintf(dest, "%d;%s;%s;%s;%s\n", new_id, nombre, apellido, anio, materia);
                                sprintf(response, "OK|Registro insertado temporalmente con ID %d.", new_id);
                            }
                        } else if (strcasecmp(command, "DELETE") == 0) {
                            char *id_str = strtok_r(NULL, "|", &saveptr1); char *nombre = strtok_r(NULL, "|", &saveptr1);
                            char *apellido = strtok_r(NULL, "|", &saveptr1); char *anio = strtok_r(NULL, "|", &saveptr1);
                            char *materia = strtok_r(NULL, "|", &saveptr1);
                            if (!id_str || !nombre || !apellido || !anio || !materia) { strcpy(response, "ERROR|Sintaxis: DELETE|<ID>|<Nombre>|<Apellido>|<Anio>|<Materia>"); }
                            else {
                                char line[1024]; int deleted = 0;
                                fputs(csv_header, dest); fprintf(dest, "\n");
                                fgets(line, sizeof(line), source); 
                                while(fgets(line, sizeof(line), source)) {
                                    line[strcspn(line, "\r\n")] = 0;
                                    char line_copy[1024]; strcpy(line_copy, line);
                                    char *saveptr2;
                                    char *line_id = strtok_r(line_copy, ";", &saveptr2);
                                    if (strcmp(line_id, id_str) == 0) {
                                        char full_record_to_match[1024];
                                        sprintf(full_record_to_match, "%s;%s;%s;%s;%s", id_str, nombre, apellido, anio, materia);
                                        if (strcmp(line, full_record_to_match) == 0) {
                                            deleted = 1; 
                                        } else {
                                            fputs(line, dest); fprintf(dest, "\n"); 
                                        }
                                    } else {
                                        fputs(line, dest); fprintf(dest, "\n");
                                    }
                                }
                                if(deleted) sprintf(response, "OK|Registro con ID %s eliminado temporalmente.", id_str);
                                else sprintf(response, "ERROR|No se encontró un registro que coincida con todos los datos para el ID %s.", id_str);
                            }
                        } else if (strcasecmp(command, "UPDATE") == 0) {
                            char *id_str = strtok_r(NULL, "|", &saveptr1); char *col_name = strtok_r(NULL, "|", &saveptr1);
                            char *new_val = strtok_r(NULL, "|", &saveptr1);
                            if (!id_str || !col_name || !new_val) { strcpy(response, "ERROR|Sintaxis: UPDATE|<ID>|<columna>|<nuevo_valor>"); }
                            else {
                                int col_idx = get_column_index(col_name);
                                if (col_idx == -1) { sprintf(response, "ERROR|Nombre de columna '%s' no válido.", col_name); }
                                else {
                                    char line[1024]; int updated = 0;
                                    fputs(csv_header, dest); fprintf(dest, "\n");
                                    fgets(line, sizeof(line), source); 
                                    while(fgets(line, sizeof(line), source)) {
                                        line[strcspn(line, "\r\n")] = 0;
                                        char line_copy[1024]; strcpy(line_copy, line);
                                        char *saveptr2;
                                        char *line_id = strtok_r(line_copy, ";", &saveptr2);
                                        if (strcmp(line_id, id_str) == 0) {
                                            char *fields[MAX_COLS]; int i = 0;
                                            strcpy(line_copy, line);
                                            char *token, *saveptr3;
                                            token = strtok_r(line_copy, ";", &saveptr3);
                                            while(token && i < column_count) { fields[i++] = token; token = strtok_r(NULL, ";", &saveptr3); }
                                            fields[col_idx] = new_val;
                                            for(int j=0; j<column_count; j++) fprintf(dest, "%s%s", fields[j], (j == column_count - 1 ? "" : ";"));
                                            fprintf(dest, "\n");
                                            updated = 1;
                                        } else {
                                            fputs(line, dest); fprintf(dest, "\n");
                                        }
                                    }
                                    if(updated) sprintf(response, "OK|Registro con ID %s actualizado temporalmente.", id_str);
                                    else sprintf(response, "INFO|No se encontró el registro con ID %s para actualizar.", id_str);
                                }
                            }
                        }
                        fclose(source); fclose(dest);
                        remove(temp_file_name); 
                        rename(next_temp_file, temp_file_name); 
                        strcpy(current_data_source, temp_file_name);
                    }
                }
            }
        } else if (strncmp(temp_buffer, "FIND", 4) == 0) {
            struct flock lock_check;
            lock_check.l_type = F_WRLCK; lock_check.l_whence = SEEK_SET; lock_check.l_start = 0; lock_check.l_len = 0;
            fcntl(file_fd, F_GETLK, &lock_check);
            if (lock_check.l_type != F_UNLCK && !in_transaction) { strcpy(response, "ERROR|Hay una transacción activa. No se pueden realizar consultas."); }
            else {
                const char *source_to_read = in_transaction ? current_data_source : CSV_FILE;
                char *argument = strtok_r(temp_buffer, "|", &saveptr1) ? strtok_r(NULL, "|", &saveptr1) : NULL;
                if (argument != NULL && strcasecmp(argument, "ALL") == 0) {
                    FILE *fs = fopen(source_to_read, "r");
                    if (fs) {
                        char line_buffer[1024];
                        fgets(line_buffer, sizeof(line_buffer), fs);
                        sprintf(response, "DATA|\n%s\n", csv_header);
                        while(fgets(line_buffer, sizeof(line_buffer), fs) != NULL) {
                            if(strlen(response) + strlen(line_buffer) < MAX_BUFFER) { strcat(response, line_buffer); }
                            else { strcat(response, "...\n[DATA TRUNCATED]"); break; }
                        }
                        fclose(fs);
                    } else { strcpy(response, "ERROR|No se pudo abrir el archivo de datos."); }
                } else {
                    char *column_to_find = argument; char *value_to_find = strtok_r(NULL, "|", &saveptr1);
                    if (!column_to_find || !value_to_find) { strcpy(response, "ERROR|Sintaxis incorrecta. Use FIND|<columna>|<valor>."); }
                    else {
                        int col_index = get_column_index(column_to_find);
                        if (col_index == -1) { sprintf(response, "ERROR|Nombre de columna '%s' no válido.", column_to_find); }
                        else {
                            FILE *fs = fopen(source_to_read, "r");
                            if (fs) {
                                char line_buffer[1024]; int matches = 0;
                                sprintf(response, "DATA|\n%s\n", csv_header);
                                fgets(line_buffer, sizeof(line_buffer), fs);
                                while(fgets(line_buffer, sizeof(line_buffer), fs) != NULL) {
                                    char *line_copy = strdup(line_buffer); 
                                    char *saveptr2;
                                    char *field = strtok_r(line_copy, ";", &saveptr2);
                                    int current_col = 0; int found_in_line = 0;
                                    while (field != NULL) {
                                        if (current_col == col_index) {
                                            field[strcspn(field, "\r\n")] = 0;
                                            if (strcasecmp(field, value_to_find) == 0) { found_in_line = 1; }
                                            break;
                                        }
                                        field = strtok_r(NULL, ";", &saveptr2); current_col++;
                                    }
                                    free(line_copy);
                                    if (found_in_line) {
                                        if(strlen(response) + strlen(line_buffer) < MAX_BUFFER) { strcat(response, line_buffer); matches++; }
                                        else { strcat(response, "...\n[DATA TRUNCATED]"); break; }
                                    }
                                }
                                fclose(fs);
                                if (matches == 0) { strcpy(response, "INFO|No se encontraron coincidencias."); }
                            } else { strcpy(response, "ERROR|No se pudo abrir el archivo de datos."); }
                        }
                    }
                }
            }
        } else if (strcmp(temp_buffer, "EXIT") == 0) { break; }
        send(client_socket, response, strlen(response), 0);
    }
    
    printf(">> Cliente %s:%d desconectado.\n", client_ip, client_port);
    if (in_transaction) {
        printf(">> Cliente %s:%d desconectado en transacción. Revirtiendo cambios.\n", client_ip, client_port);
        remove(temp_file_name);
        struct flock lock = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
        fcntl(file_fd, F_SETLK, &lock);
        pthread_mutex_unlock(&file_lock_mutex);
    }
    close(client_socket); free(args);
    pthread_mutex_lock(&count_mutex);
    active_clients_count--;
    pthread_cond_signal(&queue_cond);
    print_server_status();
    pthread_mutex_unlock(&count_mutex);
    return NULL;
}


int main(int argc, char *argv[]) {
    if (argc != 4) { fprintf(stderr, "Uso: %s <puerto> <clientes_concurrentes> <clientes_en_espera>\n", argv[0]); return 1; }
    int port = atoi(argv[1]);
    max_concurrent_clients_config = atoi(argv[2]);
    max_waiting_clients_config = atoi(argv[3]);

    if (access(CSV_FILE, F_OK) == -1) { fprintf(stderr, "Error: No se encuentra la base de datos ('%s').\n", CSV_FILE); return 1; }
    load_csv_header();
    
    file_fd = open(CSV_FILE, O_RDWR);
    if (file_fd == -1) { 
        perror("No se pudo abrir el archivo CSV para bloqueo"); 
        exit(EXIT_FAILURE); 
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) { perror("Error en bind"); exit(EXIT_FAILURE); }
    listen(server_socket, max_waiting_clients_config);
    printf("Servidor iniciado. Escriba 'EXIT' y presione Enter para apagar.\n");
    print_server_status();
    
    fd_set read_fds;
    while (!shutdown_flag) {
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        if (select(server_socket + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("Error en select");
            break;
        }
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char command[256];
            if (read(STDIN_FILENO, command, sizeof(command)) > 0) {
                if (strncmp(command, "EXIT", 4) == 0) {
                    printf(">> Apagando el servidor...\n");
                    shutdown_flag = 1;
                    break;
                }
            }
        }
        if (FD_ISSET(server_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
            if (client_socket < 0) { perror("accept failed"); continue; }
            pthread_t thread_id;
            thread_args_t *args = malloc(sizeof(thread_args_t));
            args->client_socket = client_socket;
            args->client_address = client_addr;
            if (pthread_create(&thread_id, NULL, handle_client, (void *)args) != 0) {
                perror("No se pudo crear el hilo");
                free(args);
                close(client_socket);
            }
            pthread_detach(thread_id);
        }
    }
    pthread_mutex_lock(&count_mutex);
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&count_mutex);
    sleep(1);
    close(file_fd);
    close(server_socket);
    printf(">> Servidor apagado.\n");
    return 0;
}