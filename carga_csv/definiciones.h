#ifndef DEFINICIONES_H
#define DEFINICIONES_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>       // Para shmget(), shmat(), shmdt(), shmctl()
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_STR 10
#define CLAVE_COLA 1234  // clave única para la cola
#define TOTAL_IDS 10

// Estructura de alumno
typedef struct {
    int id;
    char nombre[MAX_STR + 1];   // +1 para el '\0' (fin de cadena)
    char apellido[MAX_STR + 1];
    int anio;
    char materia[MAX_STR + 1];
} Alumno;

// Estructura de mensaje (System V: mtype debe ser el primer campo y > 0)
typedef struct {
    long mtype;
    int generador_id;
} Mensaje;

// Estructura para lista de IDs
typedef struct {
    int ids[TOTAL_IDS];
    int cantidad;
} ListaIDs;

// ---- Estructura de parámetros ----
typedef struct {
    int generadores;        // -g N (>=1)
    int total_registros;    // -r M (>=1)
    const char* salida_csv; // -o (default "datos.csv")
    int mostrar_ayuda;     // -h / --help
} Params;

// ---- Prototipos ----
int parse_params(int argc, char** argv, Params* out);
int genera_readme(const Params* p);

// Declaraciones de funciones
void funcion_prueba_parametros();
Alumno* crear_memoria_compartida();
void generador(int (*pipe_respuesta)[2], int idx_pipe, Alumno* mem_comp, int id_cola);
void coordinador(int (*pipe_respuesta)[2], Alumno* mem_comp, int cant_registros, int id_cola, int cant_generadores);
void generar_y_enviar_ids(int (*pipe_respuesta)[2], int id_generador, int cant_registros, int contador_registro, int *ultimo_id_enviado);
void enviar_lista_vacia(int (*pipe_respuesta)[2], int id_generador);
void guardarAlumnoCSV(Alumno alumno, const char* filename, int contador_registro, FILE* fp);

// Variables globales externas
extern sem_t *Mutex;
extern sem_t *capacidad_memoria;
extern sem_t *nuevo_alumno;

#endif // DEFINICIONES_H

