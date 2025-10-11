NR > 1 {
    id = $1; # El ID es la primera columna

    if (id in vistos) {
        print "Error: ID duplicado encontrado -> " id;
        errores=1;
    }
    vistos[id] = 1;

    total_ids++;
    if (min == "" || id < min) {
        min = id;
    }
    if (max == "" || id > max) {
        max = id;
    }
}
END {
    if (errores) {
        exit 1;
    }

    if (total_ids != (max - min + 1)) {
        print "Error: Los IDs no son correlativos. Hay saltos.";
        print "Total de IDs únicos: " total_ids;
        print "Rango esperado (min-max): " min "-" max;
    } else {
        print "Verificación exitosa: No hay IDs duplicados y son correlativos.";
        print "Se encontraron " total_ids " registros únicos desde el ID " min " al " max ".";
    }
}
