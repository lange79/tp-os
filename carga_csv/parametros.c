#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include "definiciones.h"

// Muestra ayuda
static void imprimir_ayuda(const char* prog) {
    fprintf(stderr,
        "Uso: %s -g <generadores> -r <total_registros> [-o <archivo.csv>] [-h|--help]\n\n"
        "Opciones:\n"
        "  -g N        Cantidad de procesos generadores (>=1)\n"
        "  -r M        Cantidad TOTAL de registros a generar (>=1)\n"
        "  -h,--help   Muestra esta ayuda\n",
        prog
    );
}

// Parsea y valida los parámetros
int parse_params(int argc, char** argv, Params* out) {
    if (!out) return 0;

    out->generadores = -1;
    out->total_registros = -1;
    out->mostrar_ayuda = 0;

    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, longidx;
    while ((opt = getopt_long(argc, argv, "g:r:h", long_opts, &longidx)) != -1) {
        switch (opt) {
            case 'g': out->generadores = atoi(optarg); break;
            case 'r': out->total_registros = atoi(optarg); break;
            case 'h': out->mostrar_ayuda = 1; break;
            default:
                imprimir_ayuda(argv[0]);
                return 0;
        }
    }

    if (out->mostrar_ayuda) {
        imprimir_ayuda(argv[0]);
        return 0;
    }

    if (out->generadores < 1) {
        fprintf(stderr, "Error: -g debe ser >= 1\n");
        imprimir_ayuda(argv[0]);
        return 0;
    }
    if (out->total_registros < 1) {
        fprintf(stderr, "Error: -r debe ser >= 1\n");
        imprimir_ayuda(argv[0]);
        return 0;
    }

    if (out->generadores > out->total_registros) {
        fprintf(stderr, "Error: hay mas generadores (%d) que registros (%d)\n",
                out->generadores, out->total_registros);
        return 0;
    }

    return 1;
}

// Genera README.md con ejemplo de uso
int genera_readme(const Params* p) {
    FILE* f = fopen("README.md", "w");
    if (!f) return 0;

    fprintf(f,
        "# TP1 - Generador de datos\n\n"
        "## Uso\n```\n"
        "./tp1 -g %d -r %d -o\n```\n",
        p ? p->generadores : 4,
        p ? p->total_registros : 100
    );

    fclose(f);
    return 1;
}

// Prueba simple
void funcion_prueba_parametros(void) {
    printf("Soy parametros\n");
}