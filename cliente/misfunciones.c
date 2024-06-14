/****************************************************************************/
/* Plantilla para implementación de funciones del cliente (rcftpclient)     */
/* $Revision$ */
/* Aunque se permite la modificación de cualquier parte del código, se */
/* recomienda modificar solamente este fichero y su fichero de cabeceras asociado. */
/****************************************************************************/

/**************************************************************************/
/* INCLUDES                                                               */
/**************************************************************************/
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "rcftp.h" // Protocolo RCFTP
#include "rcftpclient.h" // Funciones ya implementadas
#include "multialarm.h" // Gestión de timeouts
#include "vemision.h" // Gestión de ventana de emisión
#include "misfunciones.h"

/**************************************************************************/
/* VARIABLES GLOBALES                                                     */
/**************************************************************************/
#warning HAY QUE PONER EL NOMBRE (Y BORRAR EL WARNING)
// elegir 1 o 2 autores y sustituir "Apellidos, Nombre" manteniendo el formato
//char* autores="Autor: Apellidos, Nombre"; // un solo autor
char* autores="Autor: Apellidos, Nombre\nAutor: Apellidos, Nombre"; // dos autores

// variable para indicar si mostrar información extra durante la ejecución
// como la mayoría de las funciones necesitaran consultarla, la definimos global
extern char verb;


// variable externa que muestra el número de timeouts vencidos
// Uso: Comparar con otra variable inicializada a 0; si son distintas, tratar un timeout e incrementar en uno la otra variable
extern volatile const int timeouts_vencidos;


/**************************************************************************/
/* Obtiene la estructura de direcciones del servidor */
/**************************************************************************/
struct addrinfo* obtener_struct_direccion(char *dir_servidor, char *servicio, char f_verbose)
{
    struct addrinfo hints,     // Variable para especificar la solicitud
                    *servinfo, // Puntero para respuesta de getaddrinfo()
                    *direccion;// Puntero para recorrer la lista de
                               // direcciones de servinfo
    int status;     // Finalización correcta o no de la llamada getaddrinfo()
    int numdir = 1; // Contador de estructuras de direcciones en la
                    // lista de direcciones de servinfo

    // sobreescribimos con ceros la estructura
    // para borrar cualquier dato que pueda malinterpretarse
    memset(&hints, 0, sizeof hints);

    // genera una estructura de dirección con especificaciones de la solicitud
    if (f_verbose)
    {
        printf("1 - Especificando detalles de la estructura de direcciones a solicitar... \n");
        fflush(stdout);
    }

    // especificamos la familia de direcciones con la que queremos trabajar:
    // AF_UNSPEC, AF_INET (IPv4), AF_INET6 (IPv6), etc.
    hints.ai_family = AF_UNSPEC;

    if (f_verbose)
    {
        printf("\tFamilia de direcciones/protocolos: ");
        switch (hints.ai_family)
        {
            case AF_UNSPEC: printf("IPv4 e IPv6\n"); break;
            case AF_INET:   printf("IPv4\n"); break;
            case AF_INET6:  printf("IPv6\n"); break;
            default:        printf("No IP (%d)\n", hints.ai_family); break;
        }
        fflush(stdout);
    }

    // especificamos el tipo de socket deseado:
    // SOCK_STREAM (TCP), SOCK_DGRAM (UDP), etc.
    hints.ai_socktype = SOCK_DGRAM;

    if (f_verbose)
    {
        printf("\tTipo de comunicación: ");
        switch (hints.ai_socktype)
        {
            case SOCK_STREAM: printf("flujo (TCP)\n"); break;
            case SOCK_DGRAM:  printf("datagrama (UDP)\n"); break;
            default:          printf("no convencional (%d)\n", hints.ai_socktype); break;
        }
        fflush(stdout);
    }

    // flags específicos dependiendo de si queremos la dirección como cliente
    // o como servidor
    if (dir_servidor != NULL)
    {
        // si hemos especificado dir_servidor, es que somos el cliente
        // y vamos a conectarnos con dir_servidor
        if (f_verbose) printf("\tNombre/dirección del equipo: %s\n", dir_servidor);
    }
    else
    {
        // si no hemos especificado, es que vamos a ser el servidor
        if (f_verbose) printf("\tNombre/dirección: equipo local\n");

        // especificar flag para que la IP se rellene con lo necesario para hacer bind
        // consultar documentación con: 'man getaddrinfo')
        hints.ai_flags = AI_PASSIVE;
    }
    if (f_verbose) printf("\tServicio/puerto: %s\n", servicio);

    // llamada getaddrinfo() para obtener la estructura de direcciones solicitada
    // getaddrinfo() pide memoria dinámica al SO,
    // la rellena con la estructura de direcciones
    // y escribe en servinfo la dirección donde se encuentra dicha estructura.
    // La memoria dinámica reservada en una función NO se libera al salir de ella
    // Para liberar esta memoria, usar freeaddrinfo()
    if (f_verbose)
    {
        printf("2 - Solicitando la estructura de direcciones con getaddrinfo()... ");
        fflush(stdout);
    }
    status = getaddrinfo(dir_servidor, servicio, &hints, &servinfo);
    if (status != 0)
    {
        fprintf(stderr,"Error en la llamada getaddrinfo(): %s\n", gai_strerror(status));
        exit(1);
    }
    if (f_verbose) printf("hecho\n");

    // imprime la estructura de direcciones devuelta por getaddrinfo()
    if (f_verbose)
    {
        printf("3 - Analizando estructura de direcciones devuelta... \n");
        direccion = servinfo;
        while (direccion != NULL)
        {   // bucle que recorre la lista de direcciones
            printf("    Dirección %d:\n", numdir);
            printsockaddr((struct sockaddr_storage*) direccion->ai_addr);
            // "avanzamos" a la siguiente estructura de direccion
            direccion = direccion->ai_next;
            numdir++;
        }
    }

    // devuelve la estructura de direcciones devuelta por getaddrinfo()
    return servinfo;
}

/**************************************************************************/
/* Imprime una direccion */
/**************************************************************************/
void printsockaddr(struct sockaddr_storage * saddr)
{
    struct sockaddr_in *saddr_ipv4; // puntero a estructura de dirección IPv4
    // el compilador interpretará lo apuntado como estructura de dirección IPv4
    struct sockaddr_in6 *saddr_ipv6; // puntero a estructura de dirección IPv6
    // el compilador interpretará lo apuntado como estructura de dirección IPv6
    void *addr; // puntero a dirección
    // como puede ser tipo IPv4 o IPv6 no queremos que el compilador la
    // interprete de alguna forma particular, por eso void
    char ipstr[INET6_ADDRSTRLEN]; // string para la dirección en formato texto
    int port; // almacena el número de puerto al analizar estructura devuelta

    if (saddr == NULL)
    {
        printf("La dirección está vacía\n");
    }
    else
    {
        printf("\tFamilia de direcciones: ");
        fflush(stdout);
        if (saddr->ss_family == AF_INET6)
        {   // IPv6
            printf("IPv6\n");
            // apuntamos a la estructura con saddr_ipv6 (cast evita warning),
            // así podemos acceder al resto de campos a través de
            // este puntero sin más casts
            saddr_ipv6 = (struct sockaddr_in6 *)saddr;
            // apuntamos al campo de la estructura que contiene la dirección
            addr = &(saddr_ipv6->sin6_addr);
            // obtenemos el puerto, pasando del formato de red al formato local
            port = ntohs(saddr_ipv6->sin6_port);
        }
        else if (saddr->ss_family == AF_INET)
        {   // IPv4
            printf("IPv4\n");
            saddr_ipv4 = (struct sockaddr_in *)saddr;
            addr = &(saddr_ipv4->sin_addr);
            port = ntohs(saddr_ipv4->sin_port);
        }
        else
        {
            fprintf(stderr, "familia desconocida\n");
            exit(1);
        }
        // convierte la dirección ip a string
        inet_ntop(saddr->ss_family, addr, ipstr, sizeof ipstr);
        printf("\tDirección (interpretada según familia): %s\n", ipstr);
        printf("\tPuerto (formato local): %d\n", port);
    }
}
/**************************************************************************/
/* Configura el socket, devuelve el socket y servinfo */
/**************************************************************************/
int initsocket(struct addrinfo *servinfo, char f_verbose)
{
    int sock = -1;
    sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sock < 0)
    {
        perror("Error en la llamada socket: No se pudo crear el socket");
        exit(1);
    }
    return sock;
}

/**************************************************************************/
/*  Comprueba que respuesta.next sea mensaje.numseq + mensaje.len, */
/*	que no haya flags de «ocupado/abortar» en respuesta, y que si */
/*	mensaje.flags tiene marcado «fin» tambien lo tenga respuesta.flags */
/**************************************************************************/
int esLaRespuestaEsperada(struct rcftp_msg* mensaje, struct rcftp_msg* respuesta) {
	// El numero de bytes del campo next no son iguales al numero de bytes del numero de secuencia mas los 
	// de la longitud del mensaje o estan activos otros flags aparte del por defecto, muestra error
	if (htonl(respuesta->next) != (htonl(mensaje->numseq) + htons(mensaje->len)) 
		|| respuesta->flags == F_BUSY || respuesta->flags == F_ABORT 
		|| mensaje->flags == F_FIN && respuesta->flags != F_FIN) {
		fprintf(stderr, "Error, la respuesta no es la esperada\n");
		return 0;
	}
	return 1;
}
/**************************************************************************/
/* Comprueba la versión y el checksum del mensaje */
/**************************************************************************/
int mensajeValido(struct rcftp_msg* respuesta) {
	// La versión del mensaje no coincide con RCFTP_VERSION_1 o
	// la suma de verificación del mensaje es inválida, muestra error
	if (respuesta->version != RCFTP_VERSION_1 || issumvalid((void*)respuesta, sizeof(*respuesta)) == 0) {
		fprintf(stderr, "Error, el mensaje no es valido\n");
		return 0;
	}
	return 1;
}

/**************************************************************************/
/* Rellena cada uno de los campos del mensaje (struct rcftp_msg) a enviar */
/**************************************************************************/
void construirMensajeRCFTP(struct rcftp_msg* mensaje, ssize_t numseq, ssize_t datos, int ultimomensaje){
	mensaje->version = RCFTP_VERSION_1;
	mensaje->numseq = numseq;
	mensaje->len = htons(datos);

	if (ultimomensaje) {
		mensaje->flags = F_FIN;
	}
	else {
		mensaje->flags = F_NOFLAGS;
	}

	mensaje->next = htonl(0);
	mensaje->sum = 0;
	mensaje->sum = xsum((char*)mensaje,sizeof(*mensaje));
    printf("mensaje construido"); //debug
}
/**************************************************************************/
/*  algoritmo 1 (basico)  */
/**************************************************************************/
void alg_basico(int socket, struct addrinfo *servinfo) {

	struct rcftp_msg* mensaje = malloc(sizeof(struct rcftp_msg));
	struct rcftp_msg* respuesta = malloc(sizeof(struct rcftp_msg));

	int ultimoMensaje = 0;
	int ultimoMensajeConfirmado = 0;

	printf("Comunicación con algoritmo básico\n");
    printf("Antes de readtobuffer\n"); //debug
	int datos = readtobuffer((char*)mensaje->buffer, RCFTP_BUFLEN);
    printf("Despues de readtobuffer\n"); //debug

	if (datos <= 0) { // if (finDeFicheroAlcanzado) then
		ultimoMensaje = 1;
        printf("es el ultimo mensaje\n"); //debug
	}

    printf("Antes de construir mensaje\n"); //debug
	construirMensajeRCFTP(mensaje, htonl(0), datos, ultimoMensaje);
    printf("Despues de construir mensaje\n"); //debug
    fflush(stdout);
    printf("ultimo mensaje confirmado vale: %d ", ultimoMensajeConfirmado); //debug
	while (!ultimoMensajeConfirmado) {
        printf("entra en el while "); //debug
		int enviado = sendto(socket, (char*)mensaje, sizeof(*mensaje), 0, servinfo->ai_addr,servinfo->ai_addrlen);
        if (enviado == -1) {
            printf("errror al enviar mensaje (printf)"); //debug
            perror("Error al enviar mensaje");}
		int recibido = recvfrom(socket, (char*)respuesta, sizeof(*respuesta), 0, servinfo->ai_addr, &(servinfo->ai_addrlen));
        if (recibido == -1) {
            perror("Error al recibir mensaje");} //debug
		if (mensajeValido(respuesta) && esLaRespuestaEsperada(mensaje, respuesta)) {
			if (ultimoMensaje) {
				ultimoMensajeConfirmado = 1;
			}
			else {
				datos = readtobuffer((char*)mensaje->buffer, RCFTP_BUFLEN);
				if (datos <= 0) { // if (finDeFicheroAlcanzado) then
					ultimoMensaje = 1;
				}
				construirMensajeRCFTP(mensaje, respuesta->next, datos, ultimoMensaje);
			}
		}
	}
}

/**************************************************************************/
/*  algoritmo 2 (stop & wait)  */
/**************************************************************************/
void alg_stopwait(int socket, struct addrinfo *servinfo) {

	printf("Comunicación con algoritmo stop&wait\n");

#warning FALTA IMPLEMENTAR EL ALGORITMO STOP-WAIT
	printf("Algoritmo no implementado\n");
}

/**************************************************************************/
/*  algoritmo 3 (ventana deslizante)  */
/**************************************************************************/
void alg_ventana(int socket, struct addrinfo *servinfo,int window) {

	printf("Comunicación con algoritmo go-back-n\n");

#warning FALTA IMPLEMENTAR EL ALGORITMO GO-BACK-N
	printf("Algoritmo no implementado\n");
}


