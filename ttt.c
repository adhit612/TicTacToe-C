//CLIENT CODE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <strings.h>

volatile int active = 1;


int connect_inet(char *host, char *service)
{
    struct addrinfo hints, *info_list, *info;
    int sock, error;

    // look up remote host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;  // in practice, this means give us IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // indicate we want a streaming socket

    error = getaddrinfo(host, service, &hints, &info_list);
    //on success, getaddrinfo() will write to the &info_list pointer 
    //and will change it to point to the beginning of a linked list
    if (error) {
        fprintf(stderr, "error looking up %s:%s: %s\n", host, service, gai_strerror(error));
        return -1;
    }

    for (info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (sock < 0) continue;

        error = connect(sock, info->ai_addr, info->ai_addrlen); //if socket does work, let's try to open a connection to that host
        if (error) {
            close(sock);
            continue;
        }

        break;
    }
    freeaddrinfo(info_list);

    if (info == NULL) {
        fprintf(stderr, "Unable to connect to %s:%s\n", host, service);
        return -1;
    }

    return sock; //return the file descriptor
    //we now have an open file representing a network connection to the requested host
    //can use read to get in data, can use write to send over data
}

#define BUFLEN 256
#define BUFSIZE 256
#define HOSTSIZE 100
#define PORTSIZE 10
void read_data(int sock, struct sockaddr *rem, socklen_t rem_len)
{
    char buf[BUFSIZE + 1], host[HOSTSIZE], port[PORTSIZE];
    int bytes, error;

    error = getnameinfo(rem, rem_len, host, HOSTSIZE, port, PORTSIZE, NI_NUMERICSERV);
    if (error) {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
        strcpy(host, "??");
        strcpy(port, "??");
    }

    printf("Connection from %s:%s\n", host, port);

    while (active && (((bytes = (read(sock, buf, BUFSIZE))) > 0))) {
        buf[bytes] = '\0';
        printf("[%s:%s] read %d bytes |%s|\n", host, port, bytes, buf);
    }

	if (bytes == 0) {
		printf("[%s:%s] got EOF\n", host, port);
	} else if (bytes == -1) {
		printf("[%s:%s] terminating:\n", host, port);
	} else {
		printf("[%s:%s] terminating\n", host, port);
	}

    close(sock);
}


void *writeThread(void *arg){
    int sock = *(int*)arg;
    int bytes;
    char buf[BUFLEN];

    while ((bytes = (read(STDIN_FILENO, buf, BUFLEN))) > 0) {
        int writeSucess = write(sock, buf, bytes);
        //printf("write success is %d\n",writeSucess);
        // FIXME: should check whether the write succeeded!
    }
}

void *readThread(void *arg){
    int sock = *(int*)arg;
    int bytes; 
    char buf[BUFLEN];
    buf[0] = '\0';
    while((bytes = read(sock,buf,BUFLEN)) > 0){
        printf("%s\n", buf);
        memset(buf, '\0', sizeof(buf));
    }
}

int main(int argc, char **argv)
{
    pthread_t tidOne, tidTwo;
    int sock, bytes;
    char buf[BUFLEN];

    if (argc != 3) {
        printf("Specify host and service\n");
        exit(EXIT_FAILURE);
    }

    sock = connect_inet(argv[1], argv[2]);
    if (sock < 0) exit(EXIT_FAILURE);

    if(pthread_create(&tidOne,NULL,writeThread,&sock) != 0){
        perror("Error creating the write thread");
        exit(1);
    }

    if(pthread_create(&tidTwo,NULL,readThread,&sock) != 0){
        perror("Error creating the read thread");
        exit(1);
    }

    pthread_join(tidOne,NULL);
    pthread_join(tidTwo,NULL);

    close(sock);

    return EXIT_SUCCESS;
}
