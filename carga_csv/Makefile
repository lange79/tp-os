# Compilar
cargador_file: main.c coordinador.c generador.c parametros.c
	gcc -o cargador_file main.c coordinador.c generador.c parametros.c -lpthread

# Ejecutar con parámetros -r38 -g4 // predeterminado
run:
	./cargador_file -r38 -g4

# Mostrar ayuda
help:
	./cargador_file -h

# Monitorear procesos
monitor-proc:
	ps aux | grep cargador_file

# Monitorear IPC
monitor-ipc:
	ipcs -a

# Monitorear semáforos
monitor-sem:
	lsof -p $$(ps aux | grep '[c]argador_file' | awk '{print $$2}' | sort -n | head -n 1) | grep PSXSEM

# Limpiar
clean:
	rm -f cargador_file *.o
