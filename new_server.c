#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>

static int keep_running = 1;

void *thread_handler (void *args);

struct server_context {
	pthread_mutex_t lock;
};

struct work_args {
	int socket;
	struct server_context *serv;
};

/* SIGINT handler: Switches global variable to stop operation */
void int_handler() {
	keep_running = 0;
}

void error(char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {

  if (argc < 2) {
		printf("Usage: %s <Host>\n", argv[0]);
		return(0);
	}

  struct sigaction sigact;
  sigact.sa_handler = int_handler;
  sigaction(SIGINT, &sigact, NULL);
  if (keep_running) {
		struct server_context *serv = (struct server_context *) malloc(sizeof(struct server_context));

    pthread_mutex_init(&serv->lock, NULL);
    /* Ignore SIGPIPE --> If client closes connection, SIGPIPE signal produced --> Process killed --> Ignore signal */
    sigset_t sig;
		sigemptyset(&sig);
		sigaddset(&sig, SIGPIPE);
		if (pthread_sigmask(SIG_BLOCK, &sig, NULL) != 0) {
			fprintf(stderr, "ERROR: Unable to mask SIGPIPE.\n");
			return(EXIT_FAILURE);
		}

    /* Get port number */
		int port = atoi(argv[1]);

    int server_sock, client_sock;
		pthread_t thread;
		struct addrinfo hints, *res, *p;
		struct sockaddr_storage *client_add;
		int option = 1;
		socklen_t sin_size = sizeof(struct sockaddr_storage);
		struct work_args *wa;

    /* Initialize hints for getaddrinfo */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;

		if (getaddrinfo(NULL, argv[1], &hints, &res) != 0) {
			fprintf(stderr, "ERROR: getaddrinfo() failed.\n");
			pthread_exit(NULL);
		}

    for (p = res; p != NULL; p = p->ai_next) {
      	/* Create socket */
        server_sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_sock < 0) {
          fprintf(stderr, "ERROR: Could not open socket.\n");
  				continue;
        }

        /* Make address resuable */
        if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(int)) < 0) {
  				fprintf(stderr, "ERROR: Socket setsockopt() failed.\n");
  				close(server_sock);
  				continue;
  			}

        /* Bind socket to address*/
        if (bind(server_sock, p->ai_addr, p->ai_addrlen) < 0) {
  				fprintf(stderr, "ERROR: Socket bind() failed.\n");
  				close(server_sock);
  				continue;
  			}

        /* Listen */
  			if (listen(server_sock, 20) < 0) {
  				fprintf(stderr, "ERROR: Socket listen() failed.\n");
  				close(server_sock);
  				continue;
  			}

        break;
    }
    freeaddrinfo(res);
    if (p == NULL) {
			fprintf(stderr, "ERROR: Could not bind to any socket.\n");
			pthread_exit(NULL);
		}

    /* Create server directory if doesn't already exist */
		struct stat st = {0};
		if (stat("./.server_directory", &st) == -1) {
			mkdir("./.server_directory", 0744);
		}

    printf("Waiting for client...\n");

    while(1 && keep_running) {
      sigaction(SIGINT, &sigact, NULL);
			client_add = malloc(sin_size);
			/* Accept */
			if ((client_sock = accept(server_sock, (struct sockaddr *) &client_add, &sin_size)) < 0 && keep_running) {
				fprintf(stderr, "ERROR: Could not accept connection.\n");
				free(client_add);
				continue;
			}

      wa = malloc(sizeof(struct work_args));
      wa->socket = client_sock;
      wa->serv = serv;
      if (keep_running) {
        if(pthread_create(&thread, NULL, thread_handler, wa) != 0) {
					fprintf(stderr, "ERROR: Could not create thread.\n");
					free(client_add);
					free(wa);
					close(client_sock);
					close(server_sock);
					return EXIT_FAILURE;
				}
      }
    }
    pthread_mutex_destroy(&serv->lock);
		free(serv);
  }
  printf("Server shutting down.\n");
	return 0;
}

void *thread_handler(void *args) {

}
