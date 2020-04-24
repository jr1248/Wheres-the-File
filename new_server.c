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

// int exists(char*, char*);
void *thread_handler (void*);

struct server_context {
	pthread_mutex_t lock;
};

struct server {
	int socket;
	struct server_context *serv;
};

/* SIGINT handler: Switches global variable to stop operation */
void run_handler() {
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
  sigact.sa_handler = run_handler;
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
		struct server *s;

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

      s = malloc(sizeof(struct server));
      s->socket = client_sock;
      s->serv = serv;
      if (keep_running) {
        if(pthread_create(&thread, NULL, thread_handler, s) != 0) {
					fprintf(stderr, "ERROR: Could not create thread.\n");
					free(client_add);
					free(s);
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

int exists(char* path) {
	DIR* dir = opendir(path);
	if (dir == NULL) {
		printf("Could not open directory\n");
		closedir(dir);
    return -1;
	}
	closedir(dir);
	return 0;
}

void *thread_handler(void *args) {
	struct sigaction sigact;
	sigact.sa_handler = run_handler;
	sigaction(SIGINT, &sigact, NULL);

	if (keep_running) {
		struct server* s;
		struct server_context* context;
		int client_sock, sent, received, i;
		char buff[BUFSIZ];

		s = (struct server*) args;
		client_sock = s->socket;
		context = s->serv;

		pthread_detach(pthread_self());
		sigaction(SIGINT, &sigact, NULL);
		if (!keep_running) {
			printf("Server shutting down.\n");
			pthread_exit(NULL);
		}

		printf("Socket %d connected.\n", client_sock);

		received = recv(client_sock, buff, BUFSIZ - 1, 0);
		buff[received] = '\0';
		if (received <= 0) {
			fprintf(stderr, "ERROR: Server's recv() failed.\n");
			pthread_exit(NULL);
		}
		char* token = strtok(buff, ":");
		/* One mutex lock per thread */
		pthread_mutex_lock(&context->lock);
		/* Do certain WTF function based on single-char command sent from client */
		if (token[0] == 'c') {
			/* CREATE */
			token = strtok(NULL, ":");
			/* Set up project path with regard to .server_directory; basic start for every command */
			char* proj_path = (char*)malloc(strlen(token) + 22);
			if (token[strlen(token) - 1] != '/') {
				snprintf(proj_path, strlen(token) + 22, "./.server_directory/%s/", token);
			}
			else {
				snprintf(proj_path, strlen(token) + 22, "./.server_directory/%s", token);
			}
			char sending[2];
			/* If dir doesn't already exist, create it; else, send error char */
			if (exists(proj_path) == -1) {
				mkdir(proj_path, 0700);
				char* version_path = (char*)malloc(strlen(proj_path) + 9);
				snprintf(version_path, strlen(proj_path) + 9, "%sversion0", proj_path);
				mkdir(version_path, 0700);
				/* Set up version's own .Manifest, for rollback purposes */
				char mani[strlen(version_path) + 11];
				snprintf(mani, strlen(version_path) + 11, "%s/.Manifest", version_path);
				int fd_mani = open(mani, O_CREAT | O_WRONLY, 0700);
				write(fd_mani, "0\n", 2);
				close(fd_mani);
				free(version_path);
				/* Set up project-wide .Manifest */
				char* manifest_path = (char*)malloc(strlen(proj_path) + 11);
				snprintf(manifest_path, strlen(proj_path) + 11, "%s.Manifest", proj_path);
				write(fd_mani, "0\n", 2);
				close(fd_mani);
				/* Set up project-wide .History */
				char history_path[strlen(proj_path) + 10];
				snprintf(history_path, strlen(proj_path) + 10, "%s.History", proj_path);
				int hist_fd = open(history_path, O_CREAT | O_WRONLY, 0700);
				write(hist_fd, "create\n0\n\n", 10);
				close(hist_fd);
				free(manifest_path);
				free(proj_path);
				sending[0] = 'c';
				sent = send(client_sock, sending, 2, 0);
				printf("Creation of project \"%s\" successful.\n", token);
			}
			else {
				/* On failure or success, always send a specific char; client will recognize these chars to mean success or different types
				* of failure. */
				sending[0] = 'x';
				sent = send(client_sock, sending, 2, 0);
				free(proj_path);
				fprintf(stderr, "ERROR: Creation of project \"%s\" failed.\n", token);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
		}
	}
	if (!keep_running) {
		printf("Server closed.\n");
	}
	pthread_exit(NULL);
}
