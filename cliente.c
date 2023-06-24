#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include<ctype.h>

#define BUFSIZE 512

/**
 * Recibe un mensaje del servidor FTP y verifica el código de respuesta.
 * 
 * @param sd     Descriptor de socket para la conexión FTP.
 * @param code   Código de respuesta esperado.
 * @param text   Puntero a un buffer de texto opcional para almacenar el mensaje recibido.
 * @return       Devuelve true si el código de respuesta coincide con el esperado, de lo contrario, devuelve false.
 */
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer
    recv_s = recv(sd, buffer, BUFSIZE, 0);



    // error checking
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if(text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * Envía un mensaje al servidor FTP en el formato adecuado.
 * 
 * @param sd         Descriptor de socket para la conexión FTP.
 * @param operation  Operación a enviar al servidor (comando FTP).
 * @param param      Parámetro opcional para la operación.
 */
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";

    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors
    if (send(sd, buffer, strlen(buffer), 0) < 0)
        err(1, "error sending data");

}

/**
* Función: entrada simple desde el teclado.
 * Devuelve: la entrada sin la tecla ENTER.
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 *Función: proceso de inicio de sesión desde el lado del cliente.
 * sd: descriptor de socket
 **/
void authenticate(int sd) {
    char *input, desc[100];
    int code;

    // ask for user
    printf("username: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "USER", input);
    
    // relese memory
    free(input);

    // wait to receive password requirement and check for errors
    if (!recv_msg(sd, 331, desc))
        errx(1, "unexpected response from server");


    // ask for password
    printf("passwd: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "PASS", input);


    // release memory
    free(input);

    // wait for answer and process it and check for errors
    if (!recv_msg(sd, 230, desc))
        errx(1, "unexpected response from server");

}

bool port(int sd, char *ip, int port) {
    char desc[BUFSIZE];
    int code;

    // send the PORT command to the server
    sprintf(desc, "%s,%d,%d", ip, port/256, port%256);
    send_msg(sd, "PORT", desc);

    // wait for answer and process it and check for errors
    if (!recv_msg(sd, 200, desc))
        errx(1, "unexpected response from server");

    return true;
}

/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 *  la función "get" se encarga de descargar un archivo desde un servidor FTP.
 *  Establece una conexión de datos utilizando el comando "PORT", configura un 
 * socket para escuchar conexiones entrantes, envía el comando "RETR" al servidor 
 * para iniciar la transferencia del archivo, recibe y escribe los datos del archivo 
 * en un archivo local, y finaliza la transferencia cerrando los sockets y el archivo.
 **/
void get(int sd, char *file_name) {
   char buffer[BUFSIZE];
    long f_size, recv_s, r_size = BUFSIZE;
    FILE *file;
    int dsd, dsda;// data channel socket
    struct sockaddr_in addr, addr2;
    socklen_t addr_len = sizeof(addr);
    socklen_t addr2_len = sizeof(addr2);
    int puerto;
    char *ip;
    ip = (char*)malloc(13*sizeof(char));


    //Esta función se utiliza para obtener la dirección IP local 
    //y el número de puerto asociados al descriptor de socke
    getsockname(sd, (struct sockaddr *) &addr, &addr_len);
    ip = inet_ntoa(addr.sin_addr);

    //Aquí se genera aleatoriamente un número de puerto entre 1024 y 65535,
    // y se asigna a la variable
    puerto = rand()%60000+1024;

    if(!port(sd, ip, puerto)) {
       printf("Invalid server answer\n");
       return;
    }

      // listen to data channel (default idem port)
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(2, "Cannot create socket");
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_port = htons(puerto);
    if (bind(dsd, (struct sockaddr *) &addr2, sizeof(addr2)) < 0) errx(4,"Cannot bind");
    if (listen(dsd,1) < 0) errx(5, "Listen data channel error");

    // send the RETR command to the server
    send_msg(sd, "RETR", file_name);
    // check for the response
    if(!recv_msg(sd, 299, buffer)) {
       close(dsd);
       return;
    }

    // accept new connection
    dsda = accept(dsd, (struct sockaddr*)&addr2, &addr2_len);
    if (dsda < 0) {
       errx(6, "Accept data channel error");
    }

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %ld bytes", &f_size);

    // open the file to write
    file = fopen(file_name, "w");

    //receive the file
    while(true) {
       if (f_size < BUFSIZE) r_size = f_size;
       recv_s = read(dsda, buffer, r_size);
       if(recv_s < 0) warn("receive error");
       fwrite(buffer, 1, r_size, file);
       if (f_size < BUFSIZE) break;
       f_size = f_size - BUFSIZE;
    }

    // close data channel
    close(dsda);

    // close the file
    fclose(file);

    // receive the OK from the server
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // close listening socket
    close(dsd);

    return;

}

/**
 * Función: operación put 
 * @param sd 
 * @param file_name 
 * la función "put" se encarga de enviar un archivo al servidor FTP. 
 * Establece una conexión de datos utilizando el comando "PORT", configura 
 * un socket para escuchar conexiones entrantes, envía el comando "STOR" al 
 * servidor junto con el nombre del archivo y su tamaño, acepta una conexión 
 * entrante, lee el archivo y envía los datos al servidor a través del canal de 
 * datos, cierra los sockets y archivos utilizados, y espera la confirmación del 
 * servidor.
 */

void put(int sd, char *file_name) {
    char buffer[BUFSIZE];
    long f_size;
    FILE *file;
    int dsd, dsda;// data channel socket
    struct sockaddr_in addr, addr2;
    socklen_t addr_len = sizeof(addr);
    socklen_t addr2_len = sizeof(addr2);
    int puerto, bread;
    char *ip;
    char *file_data, *file_size;
    file_data = (char*)malloc(50*sizeof(char));
    file_size = (char*)malloc(25*sizeof(char));

    // check if file exists if not inform error to client
    file = fopen(file_name, "r");
    if (file == NULL){
        printf("El archivo no existe.\n");
        return;
    }

    //file length
    fseek(file, 0L, SEEK_END);
    f_size = ftell(file);
    rewind(file);
    sprintf(file_size, "//%ld",f_size);


    ip = (char*)malloc(13*sizeof(char));
    getsockname(sd, (struct sockaddr *) &addr, &addr_len);
    ip = inet_ntoa(addr.sin_addr);
    puerto = rand()%60000+1024;

    if(!port(sd, ip, puerto)) {
       printf("Invalid server answer\n");
       return;
    }

    // listen to data channel (default idem port)
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(1, "Cannot create socket");
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_port = htons(puerto);
    if (bind(dsd, (struct sockaddr *) &addr2, sizeof(addr2)) < 0) errx(4,"Cannot bind");
    if (listen(dsd,1) < 0) errx(5, "Listen data channel error");

    file_data=strcat(file_name,file_size);
    // send the STOR command to the server
    send_msg(sd, "STOR", file_data);
    // check for the response
    if(!recv_msg(sd, 150, buffer)) {
       close(dsd);
       return;
    }

    // accept new connection
    dsda = accept(dsd, (struct sockaddr*)&addr2, &addr2_len);
    if (dsda < 0) {
       errx(6, "Accept data channel error");
    }

    // send the file
    while(!feof(file)) {
        bread = fread(buffer, 1, BUFSIZE, file);
        if (write(dsda, buffer, bread) < 0) warn("Error sending data");
    }

    // close data channel
    close(dsda);

    // close the file
    fclose(file);

    // receive the OK from the server
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // close listening socket
    close(dsd);

    return;
}


/**
 * function: operation quit
 * sd: socket descriptor
 * la función "quit" envía el comando "QUIT" al servidor FTP para finalizar 
 * la conexión y espera una respuesta de confirmación del servidor. Si la 
 * respuesta recibida no es la esperada, se muestra un mensaje de error.
 **/
void quit(int sd) {
    // send command QUIT to the client
     send_msg(sd, "QUIT", NULL);
    // receive the answer from the server
    if (!recv_msg(sd, 221, NULL))
        errx(1, "unexpected response from server");

}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 *  la función "operate" establece un bucle continuo donde 
 * el usuario puede ingresar comandos. Dependiendo del comando ingresado, 
 * se ejecuta la operación correspondiente (por ejemplo, "get" para descargar 
 * un archivo) o se finaliza la conexión con el servidor (comando "quit").
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        // free(input);
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        }
        else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        }
        else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

//Auxiliar functions
/**
 * @param string 
 * @return true 
 * @return false 
 * la función "direccion_IP" verifica si una cadena de caracteres 
 * representa una dirección IP válida siguiendo ciertas reglas, como 
 * la presencia de cuatro subcadenas separadas por puntos y cada subcadena 
 * debe estar en el rango numérico de 0 a 255. La función devuelve un valor 
 * booleano que indica si la dirección IP es válida o no.
 */
bool direccion_IP(char *string){
    char *token;
    bool verificacion = true;
    int contador=0,i;
    token = (char *) malloc(strlen(string)*sizeof(char));
    strcpy(token, string);
    token = strtok(token,".");

    while(token!=NULL){
        contador++;
        i=0;
        while(*(token+i)!='\0'){
            if(!isdigit(*(token+i))) verificacion = false;
            i++;
        }
        if(atoi(token)<0||atoi(token)>255) verificacion = false;
        token=strtok(NULL,".");
    }
    if(contador!=4) verificacion = false;
    free(token);

    return verificacion;
}

/**
 * @param string 
 * @return true 
 * @return false 
 * la función "direccion_puerto" verifica si una cadena de caracteres representa 
 * un número de puerto válido. Verifica si la cadena solo contiene dígitos numéricos 
 * y si el número entero resultante está en el rango válido de 0 a 65535. La función 
 * devuelve un valor booleano que indica si el número de puerto es válido o no.
 */
bool direccion_puerto(char *string){
    bool verificacion = true;
    int i=0;
    while(*(string+i)!='\0'){
        if(!isdigit(*(string+i))) verificacion = false;
        i++;
    }
    if(atoi(string)<0||atoi(string)>65535) verificacion = false;
    return verificacion;
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int sd;
    struct sockaddr_in addr;

    // arguments checking
        if(argc!=3){
        errx(1, "Error in arguments number");
    }
    if(!direccion_IP(argv[1]))
        errx(1, "Invalidad IP");
    if(!direccion_puerto(argv[2]))
        errx(1, "Invalidad Port");


    // create socket and check for errors
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0)
        err(1, "socket failed");
    
    // set socket data  
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_addr.s_addr = inet_addr(argv[1]);  

    // connect and check for errors
    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        err(1, "connect failed");
    }


    // if receive hello proceed with authenticate and operate if not warning
    if (!recv_msg(sd, 220, NULL))
        errx(1, "unexpected response from server");
    else {
        authenticate(sd);
        operate(sd);
    }

    // close socket
    close(sd);

    return 0;
}