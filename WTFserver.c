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
#include "helper.h"

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
			mkdir("./.server_directory", 0777);
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
				mkdir(proj_path, 0777);
				char* version_path = (char*)malloc(strlen(proj_path) + 9);
				snprintf(version_path, strlen(proj_path) + 9, "%sversion0", proj_path);
				mkdir(version_path, 0777);
				/* Set up version's own .Manifest, for rollback purposes */
				char mani[strlen(version_path) + 11];
				snprintf(mani, strlen(version_path) + 11, "%s/.Manifest", version_path);
				int fd_mani = open(mani, O_CREAT | O_WRONLY, 0777);
				write(fd_mani, "0\n", 2);
				close(fd_mani);
				free(version_path);
				/* Set up project-wide .Manifest */
				char* manifest_path = (char*)malloc(strlen(proj_path) + 11);
				snprintf(manifest_path, strlen(proj_path) + 11, "%s.Manifest", proj_path);
				int fd_manifest = open(manifest_path, O_CREAT | O_WRONLY, 0777);
				write(fd_manifest, "0\n", 2);
				close(fd_manifest);
				/* Set up project-wide .History */
				char history_path[strlen(proj_path) + 10];
				snprintf(history_path, strlen(proj_path) + 10, "%s.History", proj_path);
				int hist_fd = open(history_path, O_CREAT | O_WRONLY, 0777);
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
		else if (token[0] == 'd') {
			/* DESTROY */
			token = strtok(NULL, ":");
			char* path = (char*)malloc(strlen(token) + 22);
			snprintf(path, strlen(token) + 22, ".server_directory/%s", token);
			char sending[2];
			if (exists(path) == -1) {
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", token);
				sending[0] = 'x';
				sent = send(client_sock, sending, 2, 0);
				free(path);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			else {
				/* Project exists */
				int check = remove_directory(path);
				if (check == 0) {
					sending[0] = 'g';
					sent = send(client_sock, sending, 2, 0);
					free(path);
				}
				else {
					sending[0] = 'b';
					sent = send(client_sock, sending, 2, 0);
					fprintf(stderr, "ERROR: Could not remove \"%s\" project from server.\n", token);
					free(path);
					pthread_mutex_unlock(&context->lock);
					pthread_exit(NULL);
				}
			}
			printf("Project \"%s\" deleted from server.\n", token);
		}
		else if (token[0] == 'v') {
			/* CURRENTVERSION */
			token = strtok(NULL, ":");
			char* proj_path = (char*)malloc(strlen(token) + 22);
			snprintf(proj_path, strlen(token) + 22, ".server_directory/%s", token);
			if (exists(proj_path) == -1) {
				char sending[2] = "b";
				sent = send(client_sock, sending, 2, 0);
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", token);
				free(proj_path);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			// free(proj_path);
			/* Get server's copy of .Manifest for project */
			char* manifest_path = (char*)malloc(strlen(token) + 31);
			snprintf(manifest_path, strlen(token) + 31, ".server_directory/%s/.Manifest", token);
			int fd_mani = open(manifest_path, O_RDONLY);
			if (fd_mani < 0) {
				fprintf(stderr, "ERROR: Unable to open \".Manifest\" file for \"%s\" project.\n", token);
				free(manifest_path);
				free(proj_path);
				char sending[2] = "x";
				sent = send(client_sock, sending, 2, 0);
				close(fd_mani);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			struct stat s = {0};
			if (fstat(fd_mani, &s) < 0) {
				fprintf(stderr, "ERROR: fstat() failed.\n");
				char sending[2] = "x";
				sent = send(client_sock, sending, 2, 0);
				free(manifest_path);
				close(fd_mani);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			char size[256];
			snprintf(size, 256, "%d", s.st_size);
			sent = send(client_sock, size, 256, 0);
			if (sent < 0) {
				fprintf(stderr, "ERROR: Could not send size of \"%s\".\n", manifest_path);
				free(manifest_path);
				char sending[2] = "x";
				sent = send(client_sock, sending, 2, 0);
				close(fd_mani);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			char* contents = malloc(s.st_size + 1);
			int br = read(fd_mani, contents, s.st_size);
			contents[br] = '\0';
			sent = send(client_sock, contents, br, 0);
			while (sent < br) {
				int bs = send(client_sock, contents + sent, br, 0);
				sent += bs;
			}
			printf("Sent .Manifest file for \"%s\" project to client.\n", token);
			close(fd_mani);
			free(manifest_path);
		}
		else if (token[0] == 'o') {
			/* COMMIT */
			token = strtok(NULL, ":");
			char* path = (char *) malloc(strlen(token) + 22);
			snprintf(path, strlen(token) + 22, ".server_directory/%s", token);
			if (exists(path) == -1) {
				char sending[2] = "b";
				sent = send(client_sock, sending, 2, 0);
				free(path);
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", token);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			free(path);
			/* Get .Manifest */
			char* manifest_path = (char*)malloc(strlen(token) + 31);
			snprintf(manifest_path, strlen(token) + 31, ".server_directory/%s/.Manifest", token);
			char* to_send = (char*)malloc(2);
			int manifest_fd = open(manifest_path, O_RDONLY);
			int manifest_size = get_file_size(manifest_fd);
			if (manifest_fd < 0 || manifest_size < 0) {
				fprintf(stderr, "ERROR: Unable to open \".Manifest\" file for \"%s\" project.\n", token);
				free(manifest_path);
				snprintf(to_send, 2, "x");
				sent = send(client_sock, to_send, 2, 0);
				close(manifest_fd);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			int send_size = sizeof(manifest_size);
			free(to_send);
			to_send = (char*)malloc(send_size + 1);
			snprintf(to_send, send_size, "%d", manifest_size);
			sent = send(client_sock, to_send, send_size, 0);
			free(to_send);
			send_size = manifest_size;
			to_send = (char*)malloc(send_size + 1);
			int bytes_read = read(manifest_fd, to_send, send_size);
			sent = send(client_sock, to_send, bytes_read, 0);
			while (sent < bytes_read) {
				int bytes_sent = send(client_sock, to_send + sent, bytes_read, 0);
				sent += bytes_sent;
			}
			char manifest_input[bytes_read + 1];
			strcpy(manifest_input, to_send);
			/* Get .Commit data */
			char* receiving = (char*)malloc(sizeof(int));
			received = recv(client_sock, receiving, sizeof(int), 0);
			if (receiving[0] == 'x') {
				fprintf(stderr, "Client failed to create new .Commit for project \"%s\".\n", token);
				free(receiving);
				free(manifest_path);
				free(to_send);
				close(manifest_fd);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			else if (receiving[0] == 'b') {
				fprintf(stderr, "Client's copy of project \"%s\" is not up-to-date.\n", token);
				free(receiving);
				free(manifest_path);
				free(to_send);
				close(manifest_fd);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			int commit_size = atoi(receiving);
			if (commit_size == 0) {
				fprintf(stderr, "ERROR: Empty .Commit sent from client for project \"%s\".\n", token);
				close(manifest_fd);
				free(receiving);
				free(to_send);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			free(receiving);
			receiving = (char*)malloc(commit_size + 1);
			received = recv(client_sock, receiving, commit_size, 0);
			/* Create random version number to differentiate .Commits */
			srand(time(0));
			int ver = rand() % 10000;
			char* commit_path = (char*)malloc(strlen(token) + 28 + sizeof(ver));
			snprintf(commit_path, strlen(token) + 28 + sizeof(ver), ".server_directory/%s/.Commit%d", token, ver);
			int fd_comm_serv = open(commit_path, O_CREAT | O_RDWR | O_APPEND, 0777);
			free(to_send);
			to_send = (char*)malloc(2);
			if (fd_comm_serv < 0) {
				fprintf(stderr, "ERROR: Unable to create .Commit%d for \"%s\" project.\n", ver, token);
				free(commit_path);
				snprintf(to_send, 2, "b");
				sent = send(client_sock, to_send, 2, 0);
				free(receiving);
				free(to_send);
				close(manifest_fd);
				close(fd_comm_serv);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			free(commit_path);
			write(fd_comm_serv, receiving, received);
			snprintf(to_send, 2, "g");
			sent = send(client_sock, to_send, 2, 0);
			free(receiving);
			free(to_send);
			close(fd_comm_serv);
			close(manifest_fd);
			printf("Commit successful.\n");
		}
		else if (token[0] == 'p') {
			/* PUSH */
			token = strtok(NULL, ":");
			char proj[strlen(token) + 1];
			strcpy(proj, token);
			proj[strlen(token)] = '\0';
			char* proj_path = (char*)malloc(strlen(token) + 22);
			snprintf(proj_path, strlen(token) + 22, ".server_directory/%s", token);
			char* to_send = (char*)malloc(2);
			if (exists(proj_path) == -1) {
				snprintf(to_send, 2, "b");
			}
			else {
				snprintf(to_send, 2, "g");
			}
			sent = send(client_sock, to_send, 2, 0);
			char *receiving = (char*)malloc(256);
			received = recv(client_sock, receiving, 256, 0);
			int size = atoi(receiving);
			if (size == 0) {
				fprintf(stderr, "ERROR: Client's .Commit is empty for project \"%s\".\n", token);
				free(receiving);
				free(to_send);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			free(receiving);
			receiving = (char*)malloc(size + 1);
			received = recv(client_sock, receiving, size, 0);
			while (received < size) {
				int bytes_received = recv(client_sock, receiving + received, size, 0);
				received += bytes_received;
			}
			receiving[received] = '\0';

			/* Get the client's commit */
			char commit_input[strlen(receiving) + 1];
			strcpy(commit_input, receiving);
			commit_input[strlen(receiving)] = '\0';
			int commit_check = push_check(proj, commit_input);
			if (commit_check == -1) {
				free(receiving);
				snprintf(to_send, 2, "x");
				sent = send(client_sock, to_send, 2, 0);
				free(to_send);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			else if (commit_check == 1) {
				free(receiving);
				fprintf(stderr, "ERROR: Could not find matching .Commit for project \"%s\".\n", proj);
				snprintf(to_send, 2, "b");
				sent = send(client_sock, to_send, 2, 0);
				free(to_send);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			/* Open server's .Manifest */
			char manifest_path[strlen(proj) + 31];
			snprintf(manifest_path, strlen(proj) + 31, ".server_directory/%s/.Manifest", proj);
			int fd_mani = open(manifest_path, O_RDWR);
			if (fd_mani < 0) {
				free(receiving);
				snprintf(to_send, 2, "x");
				sent = send(client_sock, to_send, 2, 0);
				free(to_send);
				fprintf(stderr, "ERROR: Failed to open .Manifest for project \"%s\".\n", proj);
				close(fd_mani);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			int manifest_size = get_file_size(fd_mani);
			if (manifest_size < 0) {
				free(receiving);
				snprintf(to_send, 2, "x");
				sent = send(client_sock, to_send, 2, 0);
				free(to_send);
				fprintf(stderr, "ERROR: Failed to get .Manifest's size for project \"%s\".\n", proj);
				close(fd_mani);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			snprintf(to_send, 2, "g");
			sent = send(client_sock, to_send, 2, 0);
			/* Set up a copy of the old .Manifest in case failure and a new .Manifest with updated version number */
			char buff[manifest_size + 1];
			int br = read(fd_mani, buff, manifest_size);
			char mani[manifest_size + 1];
			char* mani_buff = (char*)malloc(manifest_size + 1);
			strncpy(mani, buff, br);
			strncpy(mani_buff, buff, br);
			char* manifest_token = strtok(buff, "\n");
			int version = atoi(manifest_token);
			mani_buff += strlen(manifest_token) + 1;
			char* wr_man = malloc(strlen(mani_buff) + 2 + sizeof(version + 1));
			snprintf(wr_man, strlen(mani_buff) + 2 + sizeof(version + 1), "%d\n%s", version + 1, mani_buff);
			close(fd_mani);
			fd_mani = open(manifest_path, O_RDWR | O_TRUNC);
			write(fd_mani, wr_man, strlen(wr_man));
			char vers_path[strlen(proj) + 29 + sizeof(version)];
			snprintf(vers_path, strlen(proj) + 29 + sizeof(version), ".server_directory/%s/version%d", proj, version);
			char vp[strlen(proj) + 29 + sizeof(version + 1)];
			snprintf(vp, strlen(proj) + 29 + sizeof(version + 1), ".server_directory/%s/version%d", proj, version + 1);
			mkdir(vp, 0777);
			int copy_check = dir_copy(vers_path, vp, 0);
			/* Make new copy of directory */
			if (copy_check == 0) {
				snprintf(to_send, 2, "g");
				sent = send(client_sock, to_send, 2, 0);
			}
			else {
				snprintf(to_send, 2, "b");
				sent = send(client_sock, to_send, 2, 0);
				free(to_send);
				free(receiving);
				close(fd_mani);
				open(manifest_path, O_WRONLY | O_TRUNC);
				write(fd_mani, mani, manifest_size);
				remove_directory(vp);
				fprintf(stderr, "ERROR: Failed to instantiate new version of project \"%s\".\n", proj);
				close(fd_mani);
				pthread_mutex_unlock(&context->lock);
				pthread_exit(NULL);
			}
			/* Tokenizing .Commit's input */
			int count = 0;
			char* commit_token;
			int i = 0, j = 0;
			int last_sep = 0;
			int tok_len = 0;
			int len = strlen(commit_input);
			int delete_check = 0, modify_check = 0;
			char* fp = NULL;
			for (i = 0; i < len; ++i) {
				if (commit_input[i] != '\t' && commit_input[i] != '\n') {
					++tok_len;
					continue;
				}
				else {
					commit_token = (char*)malloc(tok_len + 1);
					for (j = 0; j < tok_len; ++j) {
						commit_token[j] = commit_input[last_sep + j];
					}
					commit_token[tok_len] = '\0';
					last_sep += tok_len + 1;
					tok_len = 0;
					++count;
				}
				if (count % 4 == 1) {
					if (commit_token[0] == 'D') {
						delete_check = 1;
					} else if (commit_token[0] == 'M') {
						modify_check = 1;
					}
					free(commit_token);
				}
				else if (count % 4 == 2) {
					free(commit_token);
				}
				else if (count % 4 == 3) {
					fp = (char*)malloc(strlen(commit_token) + 1);
					strncpy(fp, commit_token, strlen(commit_token));
					commit_token += strlen(proj);
					int path_len = strlen(vp) + 1 + strlen(fp);
					char new_fp[path_len + 1];
					snprintf(new_fp, path_len, "%s/%s", vp, commit_token);
					if (delete_check == 1) {
						/* If D, get rid of file and mark it deleted in .Manifest */
						remove(new_fp);
						removeFile(fd_mani, fp, wr_man);
						/* Have to update .Manifest input everytime new change is made */
						int new_size = get_file_size(fd_mani);
						free(wr_man);
						wr_man = (char *) malloc(new_size + 1);
						lseek(fd_mani, 0, 0);
						int br = read(fd_mani, wr_man, new_size);
						wr_man[br] = '\0';
					}
					else {
						free(receiving);
						/* Get input of files marked A or M from client and put it in respective files */
						receiving = (char *) malloc(sizeof(int));
						received = recv(client_sock, receiving, sizeof(int), 0);
						if (receiving[0] == 'x') {
							fprintf(stderr, "ERROR: Client could not send coneents of file.\n");
							/* On failure, revert .Manifest to old version */
							close(fd_mani);
							fd_mani = open(manifest_path, O_WRONLY | O_TRUNC);
							write(fd_mani, mani, manifest_size);
							remove_directory(vp);
							free(fp);
							close(fd_mani);
							pthread_mutex_unlock(&context->lock);
							pthread_exit(NULL);
						}
						int file_size = atoi(receiving);
						free(receiving);
						receiving = (char*)malloc(file_size + 1);
						received = recv(client_sock, receiving, file_size, 0);
						while (received < file_size) {
							int bytes_received = recv(client_sock, receiving + received, file_size, 0);
							received += bytes_received;
						}
						create_dirs(new_fp, vp, 2);
						int fd2;
						if (modify_check) {
							fd2 = open(new_fp, O_WRONLY | O_TRUNC);
						}
						else {
							fd2 = open(new_fp, O_WRONLY | O_CREAT, 0777);
						}
						if (fd2 < 0) {
							free(receiving);
							snprintf(to_send, 2, "x");
							sent = send(client_sock, to_send, 2, 0);
							free(to_send);
							fprintf(stderr, "ERROR: Failed to open \"%s\" file in project.\n", new_fp);
							close(fd_mani);
							fd_mani = open(manifest_path, O_RDWR | O_TRUNC);
							write(fd_mani, mani, manifest_size);
							close(fd_mani);
							close(fd2);
							pthread_mutex_unlock(&context->lock);
							pthread_exit(NULL);
						}
						write(fd2, receiving, file_size);
						snprintf(to_send, 2, "g");
						sent = send(client_sock, to_send, 2, 0);
						close(fd2);
					}
				}
				else if (count % 4 == 0) {
					if (!delete_check) {
						/* Update .Manifest with new version or new entry */
						add(fd_mani, commit_token, fp, wr_man, 1);
						int new_size = get_file_size(fd_mani);
						free(wr_man);
						wr_man = (char *) malloc(new_size + 1);
						lseek(fd_mani, 0, 0);
						int br = read(fd_mani, wr_man, new_size);
						wr_man[br] = '\0';
						modify_check = 0;
					} else {
						delete_check = 0;
					}
					free(commit_token);
					free(fp);
				}
			}
			/* For loop finishes before last hash is recognized */
			if (tok_len > 0 && !delete_check) {
				commit_token = (char*)malloc(tok_len + 1);
				for (i = 0; i < tok_len; ++i) {
					commit_token[i] = commit_input[last_sep + i];
				}
				commit_token[tok_len] = '\0';
				if (!delete_check) {
					add(fd_mani, commit_token, fp, wr_man, 1);
					int new_size = get_file_size(fd_mani);
					free(wr_man);
					wr_man = (char *) malloc(new_size + 1);
					lseek(fd_mani, 0, 0);
					int br = read(fd_mani, wr_man, new_size);
					wr_man[br] = '\0';
				}
				free(commit_token);
				free(fp);
			}
			/* Save current .Manifest in version-specific .Manifest, for rollback purposes */
			char new_manifest_path[strlen(vp) + 11];
			snprintf(new_manifest_path, strlen(vp) + 11, "%s/.Manifest", vp);
			int fd_new_mani = open(new_manifest_path, O_CREAT | O_WRONLY, 0777);
			write(fd_new_mani, wr_man, strlen(wr_man));
			close(fd_new_mani);
			close(fd_mani);
			/* Record current .Commit to .History */
			char history_path[strlen(proj) + 30];
			snprintf(history_path, strlen(proj) + 31, ".server_directory/%s/.History", proj);
			int hist_fd = open(history_path, O_WRONLY | O_APPEND);
			write(hist_fd, "push\n", 5);
			char temp[sizeof(version + 1) + 1];
			snprintf(temp, sizeof(version + 1), "%d", version + 1);
			write(hist_fd, temp, strlen(temp));
			write(hist_fd, "\n", 1);
			write(hist_fd, commit_input, strlen(commit_input));
			write(hist_fd, "\n\n", 2);
			close(hist_fd);
			/* Create version-specific .Commit with same contents as client's .Commit ->
			 * necessary for showing rollbacks in .History */
			char new_cp[strlen(vp) + 10];
			snprintf(new_cp, strlen(vp) + 10, "%s/.Commit", vp);
			int cfd = open(new_cp, O_CREAT | O_WRONLY, 0777);
			write(cfd, commit_input, strlen(commit_input));
			close(cfd);
			printf("Push complete!\n");
			snprintf(to_send, 2, "g");
			sent = send(client_sock, to_send, 2, 0);
			free(receiving);
			free(to_send);
		}
		else if (token[0] == 'g') {
				/* UPGRADE */
				token = strtok(NULL, ":");
				char* project_path = (char*)malloc(strlen(token) + 22);
				snprintf(project_path, strlen(token) + 22, ".server_directory/%s", token);
				if (exists(project_path) == -1) {
					char sending[2] = "b";
					sent = send(client_sock, sending, 2, 0);
					fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", token);
					free(project_path);
					pthread_mutex_unlock(&context->lock);
					pthread_exit(NULL);
				}
				free(project_path);
				char* to_send = (char*)malloc(2);
				snprintf(to_send, 2, "g");
				sent = send(client_sock, to_send, 2, 0);
				/* Get .Update from client */
				char* rcvg = (char *) malloc(sizeof(int) + 1);
				received = recv(client_sock, rcvg, sizeof(int), 0);
				rcvg[received] = '\0';
				if (rcvg[0] == 'x') {
					fprintf(stderr, "ERROR: Client could not open local .Update for project \"%s\".\n", token);
					free(rcvg);
					free(to_send);
					pthread_mutex_unlock(&context->lock);
					pthread_exit(NULL);
				} else if (rcvg[0] == 'b') {
					fprintf(stderr, "ERROR: Nothing to update for project \"%s\".\n", token);
					free(rcvg);
					free(to_send);
					pthread_mutex_unlock(&context->lock);
					pthread_exit(NULL);
				}
				int update_size = atoi(rcvg);
				free(rcvg);
				rcvg = (char*)malloc(update_size + 1);
				received = recv(client_sock, rcvg, update_size, 0);
				while (received < update_size) {
					int bytes_read = recv(client_sock, rcvg + received, update_size, 0);
					received += bytes_read;
				}
				rcvg[received] = '\0';
				char update_input[received + 1];
				strcpy(update_input, rcvg);
				update_input[received] = '\0';
				/* Open .Manifest */
				char manifest_path[strlen(token) + 30];
				snprintf(manifest_path, strlen(token) + 30, ".server_directory/%s/.Manifest", token);
				int mani_fd = open(manifest_path, O_RDONLY);
				int manifest_size = get_file_size(mani_fd);
				if (mani_fd < 0 || manifest_size < 0) {
					fprintf(stderr, "ERROR: Cannot open .Manifest for project \"%s\".\n", token);
					free(rcvg);
					snprintf(to_send, 2, "m");
					sent = send(client_sock, to_send, 2, 0);
					free(to_send);
					close(mani_fd);
					pthread_mutex_unlock(&context->lock);
					pthread_exit(NULL);
				}
				char manifest_input[manifest_size + 1];
				int bytes_read = read(mani_fd, manifest_input, manifest_size);
				close(mani_fd);
				manifest_input[bytes_read] = '\0';
				char manifest_version[manifest_size + 1];
				strcpy(manifest_version, manifest_input);
				char* vers_tok = strtok(manifest_version, "\n");
				int v = atoi(vers_tok);
				char version_path[sizeof(v) + strlen(token) + 28];
				snprintf(version_path, sizeof(v) + strlen(token) + 28, ".server_directory/%s/version%d/", token, v);

				int count = 0;
				char* update_tok;
				int i = 0, j = 0;
				int last_sep = 0;
				int tok_len = 0;
				int len = strlen(update_input);
				int delete_check = 0;
				char* p = NULL;
				for (i = 0; i < len; ++i) {
					if (update_input[i] != '\t' && update_input[i] != '\n') {
						++tok_len;
						continue;
				}
				else {
					update_tok = (char*)malloc(tok_len + 1);
					for (j = 0; j < tok_len; ++j) {
						update_tok[j] = update_input[last_sep + j];
					}
					update_tok[tok_len] = '\0';
					last_sep += tok_len + 1;
					tok_len = 0;
					++count;
				}
				if (count % 4 == 1) {
					if (update_tok[0] == 'D') {
						delete_check = 1;
					}
					free(update_tok);
				}
				else if (count % 4 == 2) {
					free(update_tok);
				}
				else if (count % 4 == 3) {
					p = (char *) malloc(strlen(update_tok) + 1);
					strcpy(p, update_tok);
					p[strlen(update_tok)] = '\0';
					update_tok += strlen(token);
					if (token[strlen(token) + 1] == '/') {
						++update_tok;
					}
					char fpath[strlen(update_tok) + strlen(version_path) + 1];
					snprintf(fpath, strlen(update_tok) + strlen(version_path) + 1, "%s%s", version_path, update_tok);
					if (delete_check) {
						free(update_tok);
					}
					else {
						int fd = open(fpath, O_RDONLY);
						int file_size = get_file_size(fd);
						if (fd < 0 || file_size < 0) {
							fprintf(stderr, "ERROR: Unable to open \"%s\".\n", fpath);
							snprintf(to_send, 2, "x");
							sent = send(client_sock, to_send, 2, 0);
							free(to_send);
							free(rcvg);
							close(fd);
							close(mani_fd);
							pthread_mutex_unlock(&context->lock);
							pthread_exit(NULL);
						}
						free(to_send);
						to_send = (char*)malloc(sizeof(file_size) + 1);
						snprintf(to_send, sizeof(file_size) + 1, "%d", file_size);
						to_send[sizeof(file_size)] = '\0';
						/* Sending size */
						sent = send(client_sock, to_send, sizeof(file_size),0);
						free(to_send);
						to_send = (char *) malloc(file_size + 1);
						int br = read(fd, to_send, file_size);
						to_send[br] = '\0';

						sent = send(client_sock, to_send, file_size, 0);
						close(fd);
						free(rcvg);
						rcvg = (char*)malloc(2);
						received = recv(client_sock, rcvg, 1, 0);
						if (rcvg[0] == 'x') {
							fprintf(stderr, "ERROR: Client failed to upgrade.\n");
							free(to_send);
							free(rcvg);
							free(update_tok);
							pthread_mutex_unlock(&context->lock);
							pthread_exit(NULL);
						}
					}
				}
				else {
					free(update_tok);
				}
		}
		free(to_send);
		to_send = (char*)malloc(sizeof(int));
		snprintf(to_send, sizeof(int), "%d", manifest_size);
		sent = send(client_sock, to_send, sizeof(int), 0);
		free(to_send);
		to_send = (char *) malloc(strlen(manifest_input) + 1);
		strcpy(to_send, manifest_input);
		to_send[strlen(manifest_input)] = '\0';
		sent = send(client_sock, to_send, manifest_size, 0);
		while (sent < manifest_size) {
			int bytes_sent = send(client_sock, to_send + sent, manifest_size, 0);
			sent += bytes_sent;
		}
		received = recv(client_sock, rcvg, 2, 0);
		if (rcvg[0] == 'g') {
			printf("Upgrade successful!\n");
		} else {
			printf("Upgrade failed.\n");
		}
		close(mani_fd);
		free(to_send);
		free(rcvg);
	}
	else if (token[0] == 'u') {
		/* UPDATE */
		token = strtok(NULL, ":");
		char* project_path = (char*)malloc(strlen(token) + 22);
		snprintf(project_path, strlen(token) + 22, ".server_directory/%s", token);
		if (exists(project_path) == -1) {
			char sending[2] = "b";
			sent = send(client_sock, sending, 2, 0);
			free(project_path);
			fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", token);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		free(project_path);
		/* Send client server's .Manifest */
		char* manifest_path = (char*)malloc(strlen(token) + 31);
		snprintf(manifest_path, strlen(token) + 31, ".server_directory/%s/.Manifest", token);
		char* to_send = (char*)malloc(2);
		int mani_fd = open(manifest_path, O_RDONLY);
		int manifest_size = get_file_size(mani_fd);
		if (mani_fd < 0 || manifest_size < 0) {
			fprintf(stderr, "ERROR: Unable to open .Manifest for project \"%s\".\n", token);
			free(manifest_path);
			snprintf(to_send, 2, "x");
			sent = send(client_sock, to_send, 2, 0);
			close(mani_fd);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		int send_size = sizeof(manifest_size);
		free(to_send);
		to_send = (char*)malloc(send_size + 1);
		snprintf(to_send, send_size, "%d", manifest_size);
		sent = send(client_sock, to_send, send_size, 0);
		free(to_send);
		send_size = manifest_size;
		to_send = (char*)malloc(send_size + 1);
		int br = read(mani_fd, to_send, send_size);
		sent = send(client_sock, to_send, br, 0);
		while (sent < br) {
			int bs = send(client_sock, to_send + sent, br, 0);
			sent += bs;
		}
		printf("Sent .Manifest for project \"%s\" to client.\n", token);
		close(mani_fd);
	}
	else if (token[0] == 'r') {
		/* ROLLBACK */
		token = strtok(NULL, ":");
		char proj[strlen(token) + 1];
		strcpy(proj, token);
		proj[strlen(token)] = '\0';
		char proj_path[strlen(proj) + 22];
		snprintf(proj_path, strlen(token) + 22, ".server_directory/%s", token);
		char* to_send = (char*)malloc(2);
		if (exists(proj_path) == -1) {
			snprintf(to_send, 2, "b");
			sent = send(client_sock, to_send, 2, 0);
			fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", proj);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		char manifest_path[strlen(proj_path) + 11];
		snprintf(manifest_path, strlen(proj_path) + 11, "%s/.Manifest", proj_path);
		int fd_mani = open(manifest_path, O_RDONLY);
		if (fd_mani < 0) {
			snprintf(to_send, 2, "x");
			sent = send(client_sock, to_send, 2, 0);
			fprintf(stderr, "ERROR: Cannot open .Manifest for project \"%s\".\n", proj);
			free(to_send);
			close(fd_mani);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		/* Get version of client */
		token = strtok(NULL, ":");
		int rv = atoi(token);
		char vers[256];
		read(fd_mani, vers, 256);
		char* vers_tok = strtok(vers, "\n");
		int sv = atoi(vers_tok);
		/* Can only work if requested version is less than server's version */
		if (rv >= sv) {
			fprintf(stderr, "ERROR: Invalid version given for rollback request of project \"%s\".\n", proj);
			snprintf(to_send, 2, "v");
			sent = send(client_sock, to_send, 2, 0);
			free(to_send);
			close(fd_mani);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		int rb_check = rollback(proj_path, rv);
		if (rb_check == -1) {
			snprintf(to_send, 2, "x");
			sent = send(client_sock, to_send, 2, 0);
			free(to_send);
			close(fd_mani);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		char mp[strlen(proj_path) + 19 + sizeof(rv)];
		snprintf(mp, strlen(proj_path) + 19 + sizeof(rv), "%s/version%d/.Manifest", proj_path, rv);
		int fdm = open(mp, O_RDONLY);
		int size = get_file_size(fdm);
		if (fdm < 0 || size < 0) {
			fprintf(stderr, "ERROR: Cannot get new input for .Manifest following rollback of project \"%s\".\n", proj);
			snprintf(to_send, 2, "x");
			sent = send(client_sock, to_send, 2, 0);
			free(to_send);
			close(fd_mani);
			close(fdm);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		char nmi[size + 1];
		int br = read(fdm, nmi, size);
		nmi[br] = '\0';
		fd_mani = open(mp, O_TRUNC | O_WRONLY);
		write(fd_mani, nmi, size);
		close(fdm);
		close(fd_mani);
		snprintf(to_send, 2, "g");
		sent = send(client_sock, to_send, 2, 0);
		free(to_send);
		/* Write to .History */
		char history_path[strlen(proj_path) + 10];
		snprintf(history_path, strlen(proj_path) + 10, "%s/.History", proj_path);
		int hfd = open(history_path, O_WRONLY | O_APPEND);
		char temp[sizeof(rv) + 1];
		snprintf(temp, sizeof(rv), "%d", rv);
		write(hfd, "rollback ", 9);
		write(hfd, temp, strlen(temp));
		write(hfd, "\n", 1);
		write(hfd, temp, strlen(temp));
		write(hfd, "\n", 1);
		if (rv > 0) {
			char cpath[strlen(proj_path) + 18 + sizeof(rv)];
			snprintf(cpath, strlen(proj_path) + 18 + sizeof(rv), "%s/version%d/.Commit", proj_path, rv);
			int fd_comm = open(cpath, O_RDONLY);
			size = get_file_size(fdm);
			char commit_input[size + 1];
			br = read(fd_comm, commit_input, size);
			commit_input[br] = '\0';
			write(hfd, commit_input, strlen(commit_input));
			write(hfd, "\n\n", 2);
			close(fd_comm);
		}
		close(hfd);
		/* Success */
		printf("Rollback successful!\n");
	}
	else if (token[0] == 'h') {
		token = strtok(NULL, ":");
		char proj[strlen(token) + 1];
		strcpy(proj, token);
		proj[strlen(token)] = '\0';
		char* proj_path = (char*)malloc(strlen(token) + 22);
		snprintf(proj_path, strlen(token) + 22, ".server_directory/%s", token);
		char* to_send = (char*)malloc(2);
		if (exists(proj_path) == -1) {
			fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", proj);
			snprintf(to_send, 2, "b");
			sent = send(client_sock, to_send, 2, 0);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		char history_path[strlen(proj_path) + 10];
		snprintf(history_path, strlen(proj_path) + 10, "%s/.History", proj_path);
		int hfd = open(history_path, O_RDONLY);
		int h_size = get_file_size(hfd);
		if (h_size < 0 || hfd < 0)  {
			fprintf(stderr, "ERROR: Cannot open .History for project \"%s\".\n", proj);
			snprintf(to_send, 2, "x");
			sent = send(client_sock, to_send, 2, 0);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		snprintf(to_send, 2, "g");
		sent = send(client_sock, to_send, 1, 0);
		free(to_send);
		int send_size = sizeof(h_size);
		to_send = (char*)malloc(send_size + 1);
		snprintf(to_send, send_size, "%d", h_size);
		to_send[send_size] = '\0';
		sent = send(client_sock, to_send, send_size, 0);
		free(to_send);
		send_size = h_size;
		to_send = (char*)malloc(send_size + 1);
		int b_read = read(hfd, to_send, send_size);
		to_send[b_read] = '\0';
		sent = send(client_sock, to_send, send_size, 0);
		while (sent < send_size) {
			int b_sent = send(client_sock, to_send + sent, send_size, 0);
			sent += b_sent;
		}
		close(hfd);
		printf("Sent history of project \"%s\" to client!\n", proj);
	}
	else if (token[0] == 'k') {
		token = strtok(NULL, ":");
		char proj[strlen(token) + 1];
		strcpy(proj, token);
		proj[strlen(token)] = '\0';
		char* proj_path = (char*)malloc(strlen(token) + 22);
		snprintf(proj_path, strlen(token) + 22, ".server_directory/%s", token);
		char* to_send = (char*)malloc(2);
		if (exists(proj_path) == -1) {
			fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", proj);
			snprintf(to_send, 2, "b");
			sent = send(client_sock, to_send, 2, 0);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		/* Open .Manifest to get version number and its input */
		char manifest_path[strlen(proj_path) + 11];
		snprintf(manifest_path, strlen(proj_path) + 11, "%s/.Manifest", proj_path);
		int mfd = open(manifest_path, O_RDONLY);
		int manifest_size = get_file_size(mfd);
		if (mfd < 0 || manifest_size < 0) {
			snprintf(to_send, 2, "x");
			sent = send(client_sock, to_send, 2, 0);
			fprintf(stderr, "ERROR: Cannot open .Manifest for project \"%s\".\n", proj);
			free(to_send);
			close(mfd);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		snprintf(to_send, 2, "g");
		sent = send(client_sock, to_send, 2, 0);
		char manifest_input[manifest_size + 1];
		read(mfd, manifest_input, manifest_size);
		manifest_input[manifest_size] = '\0';

		char version[manifest_size];
		lseek(mfd, 0, 0);
		read(mfd, version, 256);
		char* version_tok = strtok(version, "\n");
		int v = atoi(version_tok);
		char vp[strlen(proj_path) + 7 + sizeof(v)];
		snprintf(vp, strlen(proj_path) + 7 + sizeof(v), "%s/version%d", proj_path, v);
		char* rcvg = (char*)malloc(sizeof(int) + 1);
		received = recv(client_sock, rcvg, sizeof(int), 0);
		int path_size = atoi(rcvg);
		free(rcvg);
		rcvg = (char*)malloc(path_size + 1);
		received = recv(client_sock, rcvg, path_size, 0);
		while (received < path_size) {
			int br = recv(client_sock, rcvg + received, path_size, 0);
			received += br;
		}
		rcvg[received] = '\0';
		int c_check = dir_copy(vp, rcvg, 1);
		if (c_check != 0) {
			free(to_send);
			to_send = (char*)malloc(2);
			snprintf(to_send, 2, "x");
			close(mfd);
			free(rcvg);
			fprintf(stderr, "ERROR: Could not copy over project \"%s\" to client.\n", token);
			pthread_mutex_unlock(&context->lock);
			pthread_exit(NULL);
		}
		free(to_send);
		/* Sending .Manifest */
		int send_size = sizeof(int);
		to_send = (char*)malloc(send_size);
		snprintf(to_send, send_size, "%d", manifest_size);
		sent = send(client_sock, to_send, send_size, 0);
		free(to_send);
		to_send = (char*)malloc(manifest_size + 1);
		strcpy(to_send, manifest_input);
		to_send[manifest_size] = '\0';
		sent = send(client_sock, to_send, manifest_size, 0);
		printf("sent: %s\n", to_send);
		while (sent < manifest_size) {
			int b_sent = send(client_sock, to_send + sent, manifest_size, 0);
			sent += b_sent;
		}
		close(mfd);
		free(rcvg);
		rcvg = (char*)malloc(2);
		received = recv(client_sock, rcvg, 2, 0);
		if (rcvg[0] == 'g') {
			printf("Sent project \"%s\" successfully to client!\n", token);
		} else {
			fprintf(stderr, "ERROR: Client could not set up local .Manifest for project \"%s\".", token);
		}
		free(rcvg);
		free(to_send);
	}
	else if (token[0] == 'x') {
		fprintf(stderr, "ERROR: Mishap on client's end.\n");
	}
	pthread_mutex_unlock(&context->lock);
	}
	if (!keep_running) {
		printf("Server closed.\n");
	}
	pthread_exit(NULL);
}
