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
#include <signal.h>
/**************************************************************************/
/* VARIABLES GLOBALES                                                     */
/**************************************************************************/

char* autores="Autor: Helali Amoura, Zineb\nAutor: Jimenez Frago, Sergio" ;

// variable para indicar si mostrar información extra durante la ejecución
// como la mayoría de las funciones necesitaran consultarla, la definimos global
extern char verb;


// variable externa que muestra el número de timeouts vencidos
// Uso: Comparar con otra variable inicializada a 0; si son distintas, tratar un timeout e incrementar en uno la otra variable
extern volatile const int timeouts_vencidos;


/**************************************************************************/
/* Obtiene la estructura de direcciones del servidor */
/**************************************************************************/
struct addrinfo* obtener_struct_direccion(char *dir_servidor, char *servicio, char f_verbose){
    struct addrinfo hints,     // variable para especificar la solicitud
                    *servinfo; // puntero para respuesta de getaddrinfo()
    struct addrinfo *direccion; // puntero para recorrer la lista de direcciones de servinfo
    int status;      // finalización correcta o no de la llamada getaddrinfo()
    int numdir = 1;  // contador de estructuras de direcciones en la lista de direcciones de servinfo

    // sobreescribimos con ceros la estructura para borrar cualquier dato que pueda malinterpretarse
    memset(&hints, 0, sizeof hints);

    // genera una estructura de dirección con especificaciones de la solicitud
    if (f_verbose)
    {
        printf("1 - Especificando detalles de la estructura de direcciones a solicitar... \n");
        fflush(stdout);
    }

    hints.ai_family = AF_UNSPEC; // opciones: AF_UNSPEC; IPv4: AF_INET; IPv6: AF_INET6; etc.

    if (f_verbose)
    {
        printf("\tFamilia de direcciones/protocolos: ");
        switch (hints.ai_family)
        {
            case AF_UNSPEC: printf("IPv4 e IPv6\n"); break;
            case AF_INET:   printf("IPv4)\n"); break;
            case AF_INET6:  printf("IPv6)\n"); break;
            default:        printf("No IP (%d)\n", hints.ai_family); break;
        }
        fflush(stdout);
    }

    hints.ai_socktype = SOCK_DGRAM; // especificar tipo de socket 

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

    // flags específicos dependiendo de si queremos la dirección como cliente o como servidor
    if (dir_servidor != NULL)
    {
        // si hemos especificado dir_servidor, es que somos el cliente y vamos a conectarnos con dir_servidor
        if (f_verbose) printf("\tNombre/dirección del equipo: %s\n", dir_servidor); 
    }
    else
    {
        // si no hemos especificado, es que vamos a ser el servidor
        if (f_verbose) printf("\tNombre/dirección: equipo local\n"); 
        hints.ai_flags = AI_PASSIVE ; // especificar flag para que la IP se rellene con lo necesario para hacer bind
    }
    if (f_verbose) printf("\tServicio/puerto: %s\n", servicio);

    // llamada a getaddrinfo() para obtener la estructura de direcciones solicitada
    // getaddrinfo() pide memoria dinámica al SO, la rellena con la estructura de direcciones,
    // y escribe en servinfo la dirección donde se encuentra dicha estructura.
    // La memoria *dinámica* reservada por una función NO se libera al salir de ella.
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
void printsockaddr(struct sockaddr_storage * saddr) {
	struct sockaddr_in  *saddr_ipv4; // puntero a estructura de dirección IPv4
    // el compilador interpretará lo apuntado como estructura de dirección IPv4
    struct sockaddr_in6 *saddr_ipv6; // puntero a estructura de dirección IPv6
    // el compilador interpretará lo apuntado como estructura de dirección IPv6
    void *addr; // puntero a dirección. Como puede ser tipo IPv4 o IPv6 no queremos que el compilador la interprete de alguna forma particular, por eso void
    char ipstr[INET6_ADDRSTRLEN]; // string para la dirección en formato texto
    int port; // para almacenar el número de puerto al analizar estructura devuelta

    if (saddr == NULL)
    { 
        printf("La dirección está vacía\n");
    }
    else
    {
        printf("\tFamilia de direcciones: ");
        fflush(stdout);
        if (saddr->ss_family == AF_INET6)
        {   //IPv6
            printf("IPv6\n");
            // apuntamos a la estructura con saddr_ipv6 (el cast evita el warning),
            // así podemos acceder al resto de campos a través de este puntero sin más casts
            saddr_ipv6 = (struct sockaddr_in6 *)saddr;
            // apuntamos a donde está realmente la dirección dentro de la estructura
            addr = &(saddr_ipv6->sin6_addr);
            // obtenemos el puerto, pasando del formato de red al formato local
            port = ntohs(saddr_ipv6->sin6_port);
        }
        else if (saddr->ss_family == AF_INET)
        {   //IPv4
            printf("IPv4\n");
            saddr_ipv4 = (struct sockaddr_in *)saddr ;
            addr = &(saddr_ipv4 -> sin_addr);
            port = ntohs(saddr_ipv4-> sin_port);
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
int initsocket(struct addrinfo *servinfo, char f_verbose){
	int sock;

    printf("\nSe usará ÚNICAMENTE la primera dirección de la estructura\n");

    // crea un extremo de la comunicación y devuelve un descriptor
    if (f_verbose)
    {
        printf("Creando el socket (socket)... ");
        fflush(stdout);
    }
    sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sock < 0)
    {
        perror("Error en la llamada socket: No se pudo crear el socket");
        /* muestra por pantalla el valor de la cadena suministrada por el programador, dos puntos y un mensaje de error que detalla la causa del error cometido */
        exit(1);
    }
    if (f_verbose) printf("hecho\n");

    return sock;
}


/**************************************************************************/
/*  algoritmo 1 (basico)  */
/**************************************************************************/

void construirMensajeRCFTP(struct rcftp_msg* mensaje, char datos[], int numseq, int len){
	mensaje->version = RCFTP_VERSION_1;
	if(len < RCFTP_BUFLEN){
		mensaje->flags = F_FIN;
	}
	else{
		mensaje->flags = F_NOFLAGS;
	}
	mensaje->numseq = htonl((uint32_t)numseq);
	mensaje->next = htonl(0);
	mensaje->len = htons((uint16_t)len);
    int i;
	for(i = 0; i < len; i++){
		mensaje->buffer[i] = datos[i];
	}
	mensaje->sum = 0;
	mensaje->sum = xsum((char*)mensaje, sizeof(*mensaje));
}

int esMensajeValido(struct rcftp_msg recvbuffer){
	int esperado = 1;
	if (recvbuffer.version != RCFTP_VERSION_1 && issumvalid(&recvbuffer, sizeof(recvbuffer)) == 0) { // versión incorrecta
		esperado = 0;
		fprintf(stderr,"Error: recibido un mensaje con versión incorrecta\n");
	}

	return esperado;
}

int esLaRespuestaEsperada(struct rcftp_msg mensaje, struct rcftp_msg respuesta){
    if (mensaje.flags == F_FIN) {
		return ((respuesta.flags == F_FIN) &&
			   (ntohl(respuesta.next) == (ntohl(mensaje.numseq) + ntohs(mensaje.len))));
	} 
    
    else {
		return ntohl(respuesta.next) == (ntohl(mensaje.numseq) + ntohs(mensaje.len)) && 
			   respuesta.flags != F_BUSY && respuesta.flags != F_ABORT;
	}
}

void alg_basico(int socket, struct addrinfo *servinfo) {
	struct rcftp_msg mensaje;
	struct rcftp_msg respuesta;
	int numseq = 0;
	char datos[RCFTP_BUFLEN];
	int ultimoMensaje = 0;
	int ultimoMensajeConfirmado = 0;
	int len = readtobuffer(datos, RCFTP_BUFLEN);

	if(len < RCFTP_BUFLEN && len >= 0){
		ultimoMensaje = 1;
	}

	construirMensajeRCFTP(&mensaje, datos, numseq, len);
  ssize_t readbytes, sentbytes;
	while(ultimoMensajeConfirmado == 0){
		sentbytes = sendto(socket,(char *)&mensaje,sizeof(mensaje),0,servinfo->ai_addr,servinfo->ai_addrlen);
        if(sentbytes < 0 || sentbytes != sizeof(mensaje)){
            printf("Error en la escritura del socket\n");
            exit(1);
        }

        printf("Mensaje enviado\n");
		    print_rcftp_msg(&mensaje, sizeof(mensaje));
        
		    readbytes = recvfrom(socket,&respuesta,sizeof(respuesta),0,servinfo->ai_addr,&(servinfo->ai_addrlen));
        if(readbytes < 0){
            printf("Error de lectura en el socket\n");
            exit(1);
        }

        printf("Mensaje recibido\n");
		    print_rcftp_msg(&respuesta, sizeof(respuesta));
        
		    if((esMensajeValido(respuesta)) && (esLaRespuestaEsperada(mensaje, respuesta))){
			    if(ultimoMensaje){
				    ultimoMensajeConfirmado = 1;
			    }
			    else{
            numseq = numseq + ntohs(mensaje.len);
            len = readtobuffer(datos, RCFTP_BUFLEN);
			  	  if(len < RCFTP_BUFLEN && len >= 0){
					    ultimoMensaje = 1;
				    }
				    construirMensajeRCFTP(&mensaje, datos, numseq, len);
			    }
		    }
	    } 
	    printf("Comunicación con algoritmo básico\n");
}

/***************************************/

/**************************************************************************/
/*  algoritmo 2 (stop & wait)  */
/**************************************************************************/
void alg_stopwait(int socket, struct addrinfo *servinfo) {
    int sockflags;
    sockflags = fcntl(socket, F_GETFL, 0); // obtiene el valor de los flags
    fcntl(socket, F_SETFL, sockflags | O_NONBLOCK); // modifica el flag de bloqueo
    signal(SIGALRM,handle_sigalrm);
    struct rcftp_msg mensaje;
	struct rcftp_msg respuesta;
	int numseq = 0;
  	char datos[RCFTP_BUFLEN];
  	int ultimoMensaje = 0;
  	int ultimoMensajeConfirmado = 0;
  	int len = readtobuffer(datos, RCFTP_BUFLEN);

  	if(len < RCFTP_BUFLEN){
  		ultimoMensaje = 1;
  	}

    construirMensajeRCFTP(&mensaje, datos, numseq, len);
    int esperar;
    int numDatosRecibidos;
    volatile int timeouts_procesados = 0;
    while(ultimoMensajeConfirmado == 0){
      sendto(socket,(char *)&mensaje,sizeof(mensaje),0,servinfo->ai_addr,servinfo->ai_addrlen);
      addtimeout();
      esperar = 1;
      while (esperar){
        numDatosRecibidos = recvfrom(socket,&respuesta,sizeof(respuesta),0,servinfo->ai_addr,&(servinfo->ai_addrlen));
        if (numDatosRecibidos > 0){
            canceltimeout();
            esperar = 0;
        }
        if (timeouts_procesados != timeouts_vencidos){
            esperar = 0;
            timeouts_procesados = timeouts_procesados + 1;
        }   
    }
    if(numDatosRecibidos != -1){
        if((esMensajeValido(respuesta)) && (esLaRespuestaEsperada(mensaje, respuesta))){
          if(ultimoMensaje){
            ultimoMensajeConfirmado = 1;
          }
          else{
            numseq = numseq + ntohs(mensaje.len);
            len = readtobuffer(datos, RCFTP_BUFLEN);
            if(len < RCFTP_BUFLEN){
              ultimoMensaje = 1;
            }
          construirMensajeRCFTP(&mensaje, datos, numseq, len);
          }
        }
      }
    }    
    printf("Comunicación con algoritmo stop&wait\n");
}

/**************************************************************************/
/*  algoritmo 3 (ventana deslizante)  */
/**************************************************************************/
int esMensajeValidoYesLaRespuestaEsperada(struct rcftp_msg recvbuffer, const int nextAnt, const int numSeq){
	int esperado = 1;
	if(recvbuffer.version != RCFTP_VERSION_1){ // versión incorrecta
		esperado = 0;
		fprintf(stderr,"Error: recibido un mensaje con versión incorrecta\n");
	}
    if(!issumvalid(&recvbuffer, sizeof(recvbuffer))){
        esperado = 0;
        fprintf(stderr,"Error: recibido un mensaje con suma incorrecta\n");
    }
    if ((ntohl(recvbuffer.next) <= nextAnt) || (ntohl(recvbuffer.next) > numSeq)){
        esperado = 0;
        fprintf(stderr,"Error: recibido un mensaje con next incorrecta\n");
    }
    if(recvbuffer.flags == F_ABORT || recvbuffer.flags == F_BUSY) {
        esperado = 0;
        fprintf(stderr,"Error: recibido un mensaje con flags incorrecta\n");
    }
	return esperado;
}

void alg_ventana(int socket, struct addrinfo *servinfo,int window) {
	printf("Comunicación con algoritmo go-back-n\n");

    //Usar el pritrcftp para ver que mensjae seesta enviando

    signal(SIGALRM,handle_sigalrm); //Reprogramar señal

    struct rcftp_msg mensaje;
	struct rcftp_msg respuesta;
    struct rcftp_msg mensajeAnterior;
  	char datos[RCFTP_BUFLEN];

    int numseq = 0;
    int numSeqAnt;
    int lenAnt = RCFTP_BUFLEN;
    int nextAnt = 0;

  	int ultimoMensaje = 0;
  	int ultimoMensajeConfirmado = 0;
    volatile int timeouts_procesados = 0;

  	int len;
    int numDatosRecibidos;
    setwindowsize(window);

    int sockflags;
    sockflags = fcntl(socket, F_GETFL, 0); // obtiene el valor de los flags
    fcntl(socket, F_SETFL, sockflags | O_NONBLOCK); // modifica el flag de bloqueo

    while(!ultimoMensajeConfirmado){
        if((getfreespace() >= RCFTP_BUFLEN) && !ultimoMensaje){
            len = readtobuffer(datos, RCFTP_BUFLEN);
            if(len < RCFTP_BUFLEN){
              ultimoMensaje = 1;
            }
            construirMensajeRCFTP(&mensaje, datos, numseq, len);
            numseq = numseq + len;
            sendto(socket,(char *)&mensaje,sizeof(mensaje),0,servinfo->ai_addr,servinfo->ai_addrlen);
            addtimeout();
            addsentdatatowindow(datos,len);
        }
        numDatosRecibidos = recvfrom(socket,&respuesta,sizeof(respuesta),0,servinfo->ai_addr,&(servinfo->ai_addrlen));
        if(numDatosRecibidos > 0){
            if(esMensajeValidoYesLaRespuestaEsperada(respuesta,nextAnt,numseq)){
                canceltimeout();
                freewindow(ntohl(respuesta.next));
                nextAnt = ntohl(respuesta.next);
                if(ultimoMensaje && (respuesta.flags == F_FIN)){
                    ultimoMensajeConfirmado = 1;
                }
            }
        }
        if(timeouts_procesados != timeouts_vencidos){
            lenAnt = RCFTP_BUFLEN;
            numSeqAnt = getdatatoresend(datos,&lenAnt);
            construirMensajeRCFTP(&mensajeAnterior, datos, numSeqAnt, lenAnt); //No estoy segura
            if(lenAnt < RCFTP_BUFLEN && ultimoMensaje){
                int si = 1;
                if(si){
                    mensajeAnterior.flags = F_FIN;
                    mensajeAnterior.sum = 0;
                    mensajeAnterior.sum = xsum((char*)&mensajeAnterior, sizeof(mensajeAnterior));
                }
            }
            sendto(socket,(char *)&mensajeAnterior,sizeof(mensajeAnterior),0,servinfo->ai_addr,servinfo->ai_addrlen);
            addtimeout();
            timeouts_procesados = timeouts_procesados + 1;
        }
    }
}
