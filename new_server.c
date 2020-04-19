#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 8081

typedef struct {
	int sock;
	struct sockaddr address;
	int addr_len;
} connection_t;

void error(char *msg) {
    perror(msg);
    exit(1);
}

void * process(void * ptr) {
	char * buffer;
	int len;
	connection_t* conn;
	long addr = 0;

	if (!ptr) pthread_exit(0);
	conn = (connection_t *)ptr;

	/* read length of message */
	read(conn->sock, &len, sizeof(int));
	if (len > 0)
	{
		addr = (long)((struct sockaddr_in *)&conn->address)->sin_addr.s_addr;
		buffer = (char *)malloc((len+1)*sizeof(char));
		buffer[len] = 0;

		/* read message */
		read(conn->sock, buffer, len);

		/* print message */
		printf("%d.%d.%d.%d: %s\n",
			(int)((addr      ) & 0xff),
			(int)((addr >>  8) & 0xff),
			(int)((addr >> 16) & 0xff),
			(int)((addr >> 24) & 0xff),
			buffer);
		free(buffer);
	}

	/* close socket and clean up */
	close(conn->sock);
	free(conn);
	pthread_exit(0);
}

int main(int argc, char *argv[]) {

    int sock = -1;
    struct sockaddr_in address;
    int port;
    connection_t * connection;
    pthread_t thread;

    /* create socket */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock <= 0) {
		    error("Cannot create socket");
    }

    /* bind socket to port */
	  address.sin_family = AF_INET;
	  address.sin_addr.s_addr = INADDR_ANY;
	  address.sin_port = htons(PORT);

    if (bind(sock, (struct sockaddr *)&address, sizeof(struct sockaddr_in)) < 0) {
  		error("Error: cannot bind socket to port");
	   }

    /* listen on port */
	  if (listen(sock, 5) < 0) {
       error("Error: cannot listen on port");
     }

    printf("Listening\n");

    while (1) {
      /* accept incoming connections */
      connection = (connection_t *)malloc(sizeof(connection_t));
      connection->sock = accept(sock, &connection->address, &connection->addr_len);
      if (connection->sock <= 0) {
        free(connection);
      }
      else {
        /* start a new thread but do not wait for it */
        pthread_create(&thread, 0, process, (void *)connection);
        pthread_detach(thread);
      }

    }

    return 0;
}
