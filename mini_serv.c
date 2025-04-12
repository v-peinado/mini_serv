#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Includes agregados
#include <sys/select.h>
#include <stdlib.h>
#include <stdio.h>

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

// --- DECLARACIÓN DE VARIABLES GLOBALES ---
// Define el número máximo de clientes (normalmente 1024, depende del sistema)
int max_client = FD_SETSIZE;
// Array para almacenar los descriptores de archivo (FD) de los clientes
// Se inicializa el primer elemento a -1 (significa socket no usado) y el resto a 0
int client[FD_SETSIZE] = {-1};
// Array para almacenar los mensajes de cada cliente
char *message[4000];
// Conjuntos de descriptores de archivo para select()
//fd_set es una estructura que contiene un conjunto de descriptores de archivo, cur significa "current" que quiere decir "actual"
fd_set cur, cur_write, cur_read;


// --- FUNCIONES AUXILIARES ---
// Función para manejo de errores: imprime mensaje y termina el programa
void ft_error()
{
	char *err = "Fatal error\n";
	write(2, err, strlen(err)); // Escribe en stderr (FD 2)
	exit(1);                    // Termina el programa con código de error
}

// Función para enviar mensaje a todos los clientes excepto al remitente
void ft_send(int fd, char *str)
{
	int len = strlen(str);
	for(int i = 0; i < max_client; i++)
	{
		// Verifica tres condiciones antes de enviar:
		// 1. El cliente existe (FD != -1)
		// 2. No es el mismo que envió el mensaje
		// 3. El socket está listo para escritura
		if(client[i] != -1 && i != fd && FD_ISSET(i, &cur_write))
			send(i, str, len, 0); // Envía el mensaje
	}
}

// Función para validar los argumentos del programa
void validate_args(int ac)
{
    // Si los argumentos son menores a 2, de error
    if (ac < 2) {
        char *err = "Wrong number of arguments\n"; 
        write(2, err, strlen(err));
        exit(1);
    }
}

// Función para configurar el socket del servidor
int setup_server_socket(char *port)
{
    int sockfd; // Descriptor de archivo del socket
    struct sockaddr_in servaddr; // Estructura de dirección del socket

    // Crear un socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        ft_error();

    // Inicializar la estructura de dirección
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(2130706433); // Dirección IP local
    servaddr.sin_port = htons(atoi(port));       // Puerto pasado como argumento

    // Vincular el socket a la dirección y puerto
    if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
        ft_error();

    // Configurar el socket para aceptar conexiones entrantes (128 es el numero maximo de conexiones en espera, es decir, la cantidad de sockets en cola que aun no han sido aceptados)
    if (listen(sockfd, 128) != 0)
        ft_error();

    return sockfd;
}

// Función para inicializar los conjuntos de FD
void init_fd_sets(int sockfd, int *max, int *index)
{
    // Inicializar el conjunto de fd y variables para el bucle principal
    FD_ZERO(&cur);
    FD_SET(sockfd, &cur);
    *max = sockfd;
    *index = 0;
}

// Función para manejar nuevas conexiones
void handle_new_connection(int sockfd, int *max, int *index)
{
    // Es el socket principal, acepta nueva conexión
    int newClient = accept(sockfd, NULL, NULL);

    // Si hubo error al aceptar, sigue con el bucle
    if (newClient <= 0)
        return;

    // Configuración del nuevo cliente:
    FD_SET(newClient, &cur);      // Añade al conjunto de monitoreo
    client[newClient] = (*index)++;  // Asigna ID único al cliente
    
    // Inicializa buffer de mensajes para el cliente
    message[newClient] = malloc(1);
    if(!message[newClient])
        ft_error();
    message[newClient][0] = 0;    // String vacío

    // Actualiza el FD máximo si es necesario
    if (newClient > *max)
        *max = newClient;

    // Notifica a todos que un nuevo cliente se ha conectado
    char str[100];
    sprintf(str, "server: client %d just arrived\n", *index - 1);
    ft_send(newClient, str);
}

// Función para manejar la desconexión de un cliente
void handle_client_disconnect(int fd)
{
    // Elimina el cliente del conjunto de monitoreo
    FD_CLR(fd, &cur);

    // Notifica a todos que el cliente se desconectó
    char str[100];
    sprintf(str, "server: client %d just left\n", client[fd]);
    ft_send(fd, str);

    // Limpia recursos del cliente
    client[fd] = -1;          // Marca como no usado
    free(message[fd]);        // Libera el buffer de mensajes
    close(fd);                // Cierra el socket
}

// Función para procesar los mensajes de un cliente
void process_client_message(int fd)
{
    // Es un cliente existente, lee datos
    char buffer[40095];
    int len = recv(fd, buffer, 40094, 0);

    // Verifica si el cliente se desconectó o hubo error
    if (len <= 0) {
        handle_client_disconnect(fd);
        return;
    } 
    
    // Añade terminador nulo al mensaje recibido
    buffer[len] = 0;
    
    // Añade el nuevo mensaje al buffer del cliente
    message[fd] = str_join(message[fd], buffer);

    char *tmp;
    // Extrae y procesa mensajes completos
    while (extract_message(&message[fd], &tmp)) {
        // Formatea y reenvía el mensaje a todos los clientes
        char str[strlen(tmp) + 100];
        sprintf(str, "client %d: %s", client[fd], tmp);
        ft_send(fd, str);
        free(tmp);
    }
}

// Función para el bucle principal del servidor
void server_main_loop(int sockfd, int max, int index)
{
    // --- BUCLE PRINCIPAL DEL SERVIDOR ---
    // implementa un servidor socket usando el patrón conocido como "multiplexación de entrada/salida" con la función select()
    while (1) {
        // Copia el conjunto principal a los conjuntos de lectura y escritura
        //Estos conjuntos (fd_set) son estructuras de datos que representan un grupo de descriptores de archivo que queremos monitorear
        cur_read = cur_write = cur;

        // select monitorea múltiples descriptores para ver cuáles están listos para operaciones de I/O.
        // max+1 porque select necesita el número más alto de FD + 1
        // excepciones y timeout estaran en NULL
        // cuando select() retorna, cur_read y cur_write contienen solo los descriptores que están listos.
        if (select(max + 1, &cur_read, &cur_write, NULL, NULL) < 0)
            continue;        // Si hay error, continúa el bucle

        // Recorre todos los posibles descriptores de archivo
        for (int fd = 0; fd <= max; fd++) {
            // Verifica si el descriptor está listo para lectura
            if (FD_ISSET(fd, &cur_read)) {
                // --- MANEJO DE NUEVAS CONEXIONES ---
                // si sockfd(socket de servidor) está listo para lectura, indica que hay una nueva conexión entrante
                if (fd == sockfd) {
                    handle_new_connection(sockfd, &max, &index);
                } 
                // --- MANEJO DE DATOS DE CLIENTES EXISTENTES ---
                else {
                    process_client_message(fd);
                }
            }
        }
    }
}

// --- FUNCIÓN PRINCIPAL ---
int main(int ac, char **av) {
    int sockfd;
    int max;
    int index;

    // --- VALIDACIÓN DE ARGUMENTOS ---
    validate_args(ac);

    // --- CONFIGURACIÓN DEL SOCKET ---
    sockfd = setup_server_socket(av[1]);

    // --- INICIALIZACIÓN DE CONJUNTOS DE DESCRIPTORES ---
    init_fd_sets(sockfd, &max, &index);

    // Iniciar el bucle principal del servidor
    server_main_loop(sockfd, max, index);

    return 0; // Nunca se alcanza este punto porque el bucle es infinito
}