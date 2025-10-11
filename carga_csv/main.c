// Ejemplo archivo
/*
ID	Nombre	Apellido	Anio	Materia
1	Homero	Simpson	3	Sistemas operativos
*/

/*
Este main solo genera procesos y llama a las funciones que deben ser desarrolladas para su funcionamiento
 */
#include "definiciones.h"
#include <sys/ipc.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

sem_t *Mutex;
sem_t *capacidad_memoria;
sem_t *nuevo_alumno;
int shmid_memoria_compartida;

Alumno* crear_memoria_compartida(){
     int shmid; // Identificador de la memoria compartida
    // Generar una clave única para la memoria compartida
    // "shmfile" debe ser un archivo existente
    // 65 es un ID arbitrario para diferenciar claves
    key_t key = ftok("shmfile", 65);

    // Crear/acceder a la memoria compartida
    // sizeof(Alumno) reserva espacio para un registro Alumno
    // 0666 → permisos lectura/escritura para todos
    // IPC_CREAT → crea la memoria si no existe
    shmid = shmget(key, sizeof(Alumno), 0666 | IPC_CREAT);
    if (shmid < 0) {
        perror("shmget"); // Imprime error si falla la creación
        return NULL;      // Termina el programa
    }

    // Guardar el ID globalmente para poder liberarlo después
    shmid_memoria_compartida = shmid;

    // Asociar la memoria compartida al espacio de direcciones del proceso
    // shm_ptr apunta a la memoria compartida
    Alumno *shm_ptr = (Alumno*) shmat(shmid, NULL, 0);
    if (shm_ptr == (void*) -1) {
        perror("shmat"); // Imprime error si falla la asociación
        return NULL;
    }

    return shm_ptr;
}

int crear_cola() {
    int id_cola = 0;
     // Acceder a la cola existente
    id_cola = msgget(CLAVE_COLA, 0666 | IPC_CREAT); //crea una cola de mensajes (o la abre si ya existe)
    if (id_cola == -1) {
        perror("No puede acceder a la cola");
        exit(1);
    }
    return id_cola;
}

int main(int argc, char** argv)
{
    // Leé parámetros (si no querés usarlos aún, igual valida)
    Params p;
    if (!parse_params(argc, argv, &p)) {
        return 1;
    }
    int cant_registros   = p.total_registros;
    int cant_generadores = p.generadores;
    
    int pipe_respuesta[cant_generadores][2];

    funcion_prueba_parametros();

    // CREAR los pipes ANTES de fork
    for (int g = 0; g < cant_generadores; g++) {
        if (pipe(pipe_respuesta[g]) < 0) {
            perror("pipe_respuesta falló");
            exit(1);
        }
    }

    Alumno *mem_comp = crear_memoria_compartida();

    int id_cola = crear_cola();

   
    // Limpiar semáforos previos si existen
    sem_unlink("/sem_mutex");
    sem_unlink("/sem_capacidad_memoria");
    sem_unlink("/sem_nuevo_alumno");
    
    // Crear semáforos con nombre
    Mutex = sem_open("/sem_mutex", O_CREAT | O_EXCL, 0600, 1);
    capacidad_memoria = sem_open("/sem_capacidad_memoria", O_CREAT | O_EXCL, 0600, 1);
    nuevo_alumno = sem_open("/sem_nuevo_alumno", O_CREAT | O_EXCL, 0600, 0);

    // crear array de pid para generadores
    pid_t *pids = malloc((cant_generadores + 1) * sizeof(pid_t));
    if (pids == NULL)
    {
        perror("malloc falló");
        return 1;
    }
    // ejemplo: mostrar el array
    for (int i = 0; i <= cant_generadores; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork falló");
            return 1;
        }
        if (pid == 0)
        {

            if(i == 0)
            {
                coordinador(pipe_respuesta, mem_comp, cant_registros, id_cola, cant_generadores);
            }
            else
            {
                // AGREGAR GENERADOR
                int idx = i - 1; // índices 0..cant_generadores-1
                generador(pipe_respuesta, idx, mem_comp, id_cola);
            }

        }
        else
        {
            // El padre guarda el PID del hijo
            pids[i] = pid;
        }
    }

    //imprimir los pids
    for (int i = 0; i <= cant_generadores; i++) {
        if(i == 0){
            printf("Coordinador, PID=%d\n", pids[i]);
        }else{
            printf("Generador, PID=%d\n", pids[i]);
        }
    }

    // Esperar Enter para terminar todos los procesos
    printf("Presiona Enter para terminar todos los procesos...\n");
    getchar();
    
    // Enviar señal SIGTERM a todos los procesos hijos (incluyendo coordinador)
    for (int i = 0; i <= cant_generadores; i++) {
        if (pids[i] > 0) {
            kill(pids[i], SIGTERM);
        }
    }
    
    // Esperar a que terminen todos los procesos hijos
    for (int i = 0; i <= cant_generadores; i++) {
        if (pids[i] > 0) {
            waitpid(pids[i], NULL, 0);
        }
    }

    // Liberar semáforos
    sem_close(Mutex);
    sem_close(capacidad_memoria);
    sem_close(nuevo_alumno);
    
    // Desasociar memoria compartida
    shmdt(mem_comp);
    
    // Eliminar segmento de memoria compartida
    shmctl(shmid_memoria_compartida, IPC_RMID, NULL);
    
    // Limpiar semáforos con nombre
    sem_unlink("/sem_mutex");
    sem_unlink("/sem_capacidad_memoria");
    sem_unlink("/sem_nuevo_alumno");

    // eliminar cola de mensajes
    msgctl(id_cola, IPC_RMID, NULL);

    // Liberar array de PIDs
    free(pids);

    printf("Finalizo\n");

    return 0;
}