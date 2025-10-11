#include "definiciones.h"
#include <signal.h>
#include <unistd.h>
#define LECTURA 0
#define ESCRITURA 1
#define ARCHIVO "alumnos.csv"

// Variable global para controlar la terminación
static volatile int terminar_proceso = 0;

// Función para manejar la señal SIGTERM
void manejar_terminacion(int sig) {
    if (sig == SIGTERM) {
        terminar_proceso = 1;
    }
}

void coordinador(int (*pipe_respuesta)[2], Alumno* mem_comp, int cant_registros, int id_cola, int cant_generadores){
    // Configurar manejador de señales
    signal(SIGTERM, manejar_terminacion);
    
    int contador_registro = 0;
    int primer_id_valido = 1;
    int contador_cola = 0;
    int contador_ids = 0;
    Mensaje msg;
    Alumno alumno;
   
    // ABRE ARCHIVO
    FILE* fp = fopen(ARCHIVO, "w");
    if (fp == NULL) {
        perror("Error abriendo archivo CSV");
        return;
    }
    // Escribir encabezado
    fprintf(fp, "id,nombre,apellido,anio,materia\n");

    while (contador_registro < cant_registros && !terminar_proceso) { // Espera a recibir mensajes encolados de todos los generadores
        // ESPERA MENSAJE DE GENERADOR
       if(contador_cola < cant_generadores){
        if (msgrcv(id_cola, &msg, sizeof(Mensaje) - sizeof(long), 0, 0) == -1) {
            perror("msgrcv");
            continue;
        }
        contador_cola++;

        if(contador_ids < cant_registros){ // solo si aún hay registros pendientes de generar
            generar_y_enviar_ids(pipe_respuesta, msg.generador_id, cant_registros, contador_ids, &primer_id_valido);
            contador_ids = primer_id_valido-1;
        }
        else{
            // Si no se envía una lista vacía, el generador queda esperando una respuesta del pipe
            enviar_lista_vacia(pipe_respuesta, msg.generador_id);
        }

       }
        
        sem_wait(nuevo_alumno);
        sem_wait(Mutex);

        alumno.id = mem_comp->id;
        snprintf(alumno.nombre, MAX_STR+1, "%s", mem_comp->nombre);
        snprintf(alumno.apellido, MAX_STR+1, "%s", mem_comp->apellido);
        alumno.anio = mem_comp->anio;
        snprintf(alumno.materia, MAX_STR+1, "%s", mem_comp->materia);
                
        sem_post(Mutex);
        sem_post(capacidad_memoria);

        //GUARDAR EN CSV
        guardarAlumnoCSV(alumno, ARCHIVO, contador_registro, fp);

        contador_registro++;
    
    }

    // Cerrar archivo CSV
    fclose(fp);
    
    // Espera la señal SIGTERM para terminar
    while(!terminar_proceso){
        sleep(1);
    }

    printf("FIN coordinador PID: %d...\n", getpid());
    exit(0);
}

void generar_y_enviar_ids(int (*pipe_respuesta)[2], int id_generador, int cant_registros, int contador_ids, int *primer_id_valido) {
    ListaIDs lista;
  
    close(pipe_respuesta[id_generador][LECTURA]);

    //se calcula la cantidad de ids a consumir y que el maximo sea TOTAL_IDS (10)
    int registros_a_consumir = (cant_registros - contador_ids < TOTAL_IDS) ? 
                          (cant_registros - contador_ids) : TOTAL_IDS;
             
  
    lista.cantidad = registros_a_consumir;

    // Crear IDs consecutivos
    for (int i = 0; i < lista.cantidad; i++) {
        lista.ids[i] = *primer_id_valido + i;
    }

    //se guarda para la siguiente vez que se generen ids
    *primer_id_valido = lista.ids[lista.cantidad - 1] + 1;

    //se envia la lista de ids al generador
    write(pipe_respuesta[id_generador][ESCRITURA], &lista, sizeof(lista));
}

void enviar_lista_vacia(int (*pipe_respuesta)[2], int id_generador){
    ListaIDs lista;
  
    close(pipe_respuesta[id_generador][LECTURA]);

    lista.cantidad = 0;
    write(pipe_respuesta[id_generador][ESCRITURA], &lista, sizeof(lista));
}

void guardarAlumnoCSV(Alumno alumno, const char* filename, int contador_registro, FILE* fp) {
    // Escribir el alumno
    fprintf(fp, "%d,%s,%s,%d,%s\n",
            alumno.id,
            alumno.nombre,
            alumno.apellido,
            alumno.anio,
            alumno.materia);

    fflush(fp); // Forzar escritura al disco
}