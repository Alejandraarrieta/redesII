#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <err.h>
#include <netinet/in.h>

#define _POSIX_C_SOURCE 200809L

#define BUFSIZE 512 // tamaño máximo para recibir los datos del cliente
#define CMDSIZE 4
#define PARSIZE 100

#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"

/**
 * Función: recv_cmd
 * ------------------
 * Lee un comando enviado por el cliente desde el descriptor de socket especificado.
 * Separa el comando y sus parámetros del buffer recibido.
 * 
 * sd: descriptor de socket para recibir el comando
 * operation: cadena de caracteres donde se almacenará el comando recibido
 * param: cadena de caracteres donde se almacenarán los parámetros del comando (si los hay)
 * 
 * return: true si se recibió y procesó correctamente el comando, false en caso contrario
 */
bool recv_cmd(int sd, char *operation, char *param) {
    char buffer[BUFSIZE], *token;
    int recv_s;

    // Recibir el comando en el buffer y manejar errores
    if ((recv_s = read(sd, buffer, BUFSIZE)) < 0) {
        warnx("Error reading buffer"); // send _ans ???
        return false;
    }
    if (recv_s == 0) {
        warnx("Empty buffer");
        return false;
    }

    // Eliminar los caracteres de terminación del buffer
    buffer[strcspn(buffer, "\r\n")] = 0;

    // Analizar el buffer para extraer el comando y los parámetros
    token = strtok(buffer, " ");
    if (token == NULL || strlen(token) < 4) {
        warn("not valid ftp command");
        return false;
    } else {
        if (operation[0] == '\0') strcpy(operation, token);
        if (strcmp(operation, token)) {
            warn("abnormal client flow: did not send %s command", operation);
            return false;
        }
        token = strtok(NULL, " ");
        if (token != NULL) strcpy(param, token);
    }
    return true;
}

/**
 * Función: send_ans
 * -----------------
 * Envía una respuesta al cliente a través del descriptor de socket especificado.
 * 
 * sd: descriptor de socket para enviar la respuesta
 * message: cadena de caracteres de la respuesta formateada
 * ...: argumentos variables para formatear la cadena de caracteres
 * 
 * return: true si se envió correctamente la respuesta, false en caso contrario
 */
bool send_ans(int sd, char *message, ...) {
    char buffer[BUFSIZE];

    va_list args;
    va_start(args, message);

    vsprintf(buffer, message, args);
    va_end(args);

    // Enviar la respuesta preformateada y verificar errores
    if (write(sd, buffer, strlen(buffer)) < 0) {
        warn("Error sending message");
        return false;
    }

    return true;
}

/**
 * Función: retr
 * -------------
 * Maneja el comando RETR (retrieve) para enviar un archivo al cliente.
 * Abre el archivo, envía su contenido al cliente y cierra el archivo.
 * 
 * sd: descriptor de socket para enviar el archivo
 * file_path: ruta del archivo a enviar
 */
void retr(int sd, char *file_path) {
    FILE *file;
    int bread;
    long fsize;
    char buffer[BUFSIZE];

    // Verificar si el archivo existe; si no, informar error al cliente
    file = fopen(file_path, "r");
    if (file == NULL) {
        warn("Error opening file");
        send_ans(sd, MSG_550, file_path);
        return;
    }

    // Enviar un mensaje de éxito con el tamaño del archivo
    fseek(file, 0L, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0L, SEEK_SET);
    send_ans(sd, MSG_299, file_path, fsize);

    // Importante retraso para evitar problemas con el tamaño del búfer
    sleep(1);

    // Enviar el archivo
    while ((bread = fread(buffer, 1, BUFSIZE, file)) > 0) {
        if (write(sd, buffer, bread) < 0) {
            warn("Error sending file");
            fclose(file);
            return;
        }
    }

    // Cerrar el archivo
    fclose(file);

    // Enviar un mensaje de transferencia completada
    send_ans(sd, MSG_226);
}

/**
 * Función: check_credentials
 * --------------------------
 * Verifica las credenciales de usuario y contraseña proporcionadas.
 * Busca la combinación de usuario y contraseña en un archivo "ftpusers".
 * 
 * user: nombre de usuario a verificar
 * pass: contraseña a verificar
 * 
 * return: true si las credenciales son válidas, false en caso contrario
 */
bool check_credentials(char *user, char *pass) {
    FILE *file;
    char *path = "./ftpusers", *line = NULL, credentials[100];
    size_t line_size = 0;
    bool found = false;

    // Crear la cadena de credenciales
    sprintf(credentials, "%s:%s", user, pass);

    // Verificar si el archivo "ftpusers" está presente
    if ((file = fopen(path, "r")) == NULL) {
        warn("Error opening %s", path);
        return false;
    }

    // Buscar la cadena de credenciales
    while (getline(&line, &line_size, file) != -1) {
        strtok(line, "\n");
        if (strcmp(line, credentials) == 0) {
            found = true;
            break;
        }
    }

    // Cerrar el archivo y liberar cualquier puntero necesario
    fclose(file);
    if (line) free(line);

    // Devolver el estado de búsqueda
    return found;
}

/**
 * Función: authenticate
 * ---------------------
 * Autentica al cliente verificando las credenciales proporcionadas.
 * Espera recibir los comandos USER y PASS del cliente y los verifica.
 * 
 * sd: descriptor de socket para comunicarse con el cliente
 * 
 * return: true si la autenticación es exitosa, false en caso contrario
 */

bool authenticate(int sd) {
    char user[PARSIZE], pass[PARSIZE];

    // Esperar a recibir el comando USER
    if (!recv_cmd(sd, "USER", user)) return false;

    // Solicitar contraseña
    send_ans(sd, MSG_331, user);

    // Esperar a recibir el comando PASS
    if (!recv_cmd(sd, "PASS", pass)) return false;

    // Si las credenciales no son válidas, denegar el inicio de sesión
    if (!check_credentials(user, pass)) {
        send_ans(sd, MSG_530);
        return false;
    }

    // Confirmar inicio de sesión
    send_ans(sd, MSG_230);
    return true;
}

/**
 * Función: operate
 * ----------------
 * Maneja la operación principal del servidor FTP.
 * Espera recibir comandos del cliente y los procesa en un bucle infinito.
 * Soporta los comandos RETR (retrieve) y QUIT.
 * 
 * sd: descriptor de socket para comunicarse con el cliente
 */
void operate(int sd) {
    char op[CMDSIZE], param[PARSIZE];

    while (true) {
        op[0] = param[0] = '\0';

        // Verificar si se reciben comandos del cliente; si no, informar y salir
        if (!recv_cmd(sd, op, param)) {
            send_ans(sd, MSG_221);
            break;
        }

        if (strcmp(op, "RETR") == 0) {
            retr(sd, param);
        } else if (strcmp(op, "QUIT") == 0) {
            // Enviar mensaje de despedida y cerrar la conexión
            send_ans(sd, MSG_221);
            close(sd);
            break;
        } else {
            // Comando inválido
            // send_ans(sd, MSG_500);
            // Uso futuro
            // send_ans(sd, MSG_502);
        }
    }
}

int main(int argc, char *argv[]) {
    // Verificación de argumentos
    if (argc < 2) {
        errx(1, "Port expected as argument");
    } else if (argc > 2) {
        errx(1, "Too many arguments");
    }

    // Reservar espacio para sockets y variables
    int master_sd, slave_sd;
    struct sockaddr_in master_addr, slave_addr;

    // Crear el socket del servidor y comprobar errores
    if ((master_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err(1, "Error creating socket");
    }

    // Establecer las opciones del socket maestro
    int optval = 1;
    if (setsockopt(master_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        err(1, "Error setting socket options");
    }

    // Asignar dirección al socket maestro y comprobar errores
    memset(&master_addr, 0, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(atoi(argv[1]));
    master_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(master_sd, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
        err(1, "Error binding socket");
    }

    // Establecer el socket en modo de escucha
    if (listen(master_sd, SOMAXCONN) < 0) {
        err(1, "Error listening on socket");
    }

    // Bucle principal
    while (true) {
        // Aceptar conexiones secuencialmente y comprobar errores
        socklen_t slave_addr_len = sizeof(slave_addr);
        if ((slave_sd = accept(master_sd, (struct sockaddr *)&slave_addr, &slave_addr_len)) < 0) {
            err(1, "Error accepting connection");
        }

        // Enviar saludo al cliente
        send_ans(slave_sd, MSG_220);

        // Autenticar al cliente
        if (authenticate(slave_sd)) {
            // Operar solo si la autenticación es exitosa
            operate(slave_sd);
        }

        // Cerrar el socket del cliente
        close(slave_sd);
    }

    // Cerrar el socket del servidor
    close(master_sd);

    return 0;
}
