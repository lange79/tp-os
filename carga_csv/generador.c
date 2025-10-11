#include "definiciones.h"
#include <signal.h>
#include <time.h>
#include <stdlib.h>

// Arrays constantes con datos de prueba
static const char* nombres[] = {
    "Juan", "María", "Carlos", "Ana", "Luis",
    "Carmen", "Pedro", "Laura", "Miguel", "Sofia"
};

static const char* apellidos[] = {
    "García", "Rodríguez", "González", "Fernández", "López",
    "Martínez", "Sánchez", "Pérez", "Gómez", "Martín"
};

static const int anios[] = {
    1, 2, 3, 4, 5
};

static const char* materias[] = {
    "Matemáticas", "Física", "Química", "Biología", "Historia",
    "Literatura", "Geografía", "Inglés", "Filosofía", "Arte"
};

// Variable global para controlar la terminación
static volatile int terminar_proceso = 0;

// Función para seleccionar un valor aleatorio de un array de strings
static const char* seleccionar_aleatorio_string(const char* array[], int tamano) {
    return array[rand() % tamano];
}

// Función para seleccionar un valor aleatorio de un array de enteros
static int seleccionar_aleatorio_int(const int array[], int tamano) {
    return array[rand() % tamano];
}

// Función para manejar la señal SIGTERM
void manejar_terminacion_generador(int sig) {
    if (sig == SIGTERM) {
        terminar_proceso = 1;
    }
}

void generador(int (*pipe_respuesta)[2], int idx_pipe, Alumno* mem_comp, int id_cola)
{
    // Configurar manejador de señales
    signal(SIGTERM, manejar_terminacion_generador);
    
    // Inicializar generador de números aleatorios
    srand(time(NULL) + getpid());
    
    int lectura = 0;
    int escritura = 1;

    //cierro los extremos que no voy a usar de cada pipe
    close(pipe_respuesta[idx_pipe][escritura]);

    Mensaje msg;
    msg.mtype = 1; // un único tipo de mensaje
    msg.generador_id = idx_pipe;

    if (msgsnd(id_cola, &msg, sizeof(Mensaje) - sizeof(long), 0) == -1) {
        perror("fallo pedidos de ids");
    }
    
    //Espero la respuesta con los ids y lo guardo en la variable ids
    ListaIDs lista;
    read(pipe_respuesta[idx_pipe][lectura], &lista, sizeof(lista));

    //Escribo el registro de alumno en la memoria compartida
    for (int i = 0; i < lista.cantidad && !terminar_proceso; i++)
    {
        sem_wait(capacidad_memoria);
        sem_wait(Mutex);
        mem_comp->id = lista.ids[i];
        snprintf(mem_comp->nombre, MAX_STR+1, "%s", seleccionar_aleatorio_string(nombres, 10));
        snprintf(mem_comp->apellido, MAX_STR+1, "%s", seleccionar_aleatorio_string(apellidos, 10));
        mem_comp->anio = seleccionar_aleatorio_int(anios, 5);
        snprintf(mem_comp->materia, MAX_STR+1, "%s", seleccionar_aleatorio_string(materias, 10));
        sem_post(Mutex);
        sem_post(nuevo_alumno);
    }
    
    // Espera la señal SIGTERM para terminar
    while(!terminar_proceso){
        sleep(1);
    }

    printf("FIN generador PID: %d...\n", getpid());
    exit(0);
}
