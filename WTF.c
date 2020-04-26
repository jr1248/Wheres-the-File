#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <openssl/sha.h>
#include "helper.h"

// method to print error msg
void error(char *msg){
  perror(msg);
  exit(0);
}

int main(int argc, char const *argv[]){
  // char IP[30];
  // char port[6];

  if (argc < 2) {
    error("ERROR: Not enough arguments");
  }

  if (strcmp("configure", argv[1]) == 0) {
    if (argc < 4) {
      error("ERROR: Need IP address and port number");
    }
    else if (argc > 4) {
      error("ERROR: Too many arguments");
    }

    /* Store IP address and port number in a file */
    int fd = open("./.configure", O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
      error("ERROR: Need to configure");
    }

    write(fd, argv[2], strlen(argv[2]));
    write(fd, "\n", 1);
    write(fd, argv[3], strlen(argv[3]));
		write(fd, "\n", 1);
    close(fd);
    printf("Configuration successful.\n");
  }
  else if (strcmp("add", argv[1]) == 0 || strcmp("remove", argv[1]) == 0) {
    /* Command-specific argument check at the beginning of every command */
		if (argc < 4) {
			fprintf(stderr, "ERROR: Need project name and file name.\n");
			return EXIT_FAILURE;
		} else if (argc > 4) {
			fprintf(stderr, "ERROR: Too many arguments.\n");
			return EXIT_FAILURE;
		}
    /* Ensure first argument is a directory */
    struct stat s;
    if (stat(argv[2], &s) != 0) {
			fprintf(stderr, "ERROR: Project \"%s\" does not exist.\n", argv[2]);
			return EXIT_FAILURE;
		}
    int file = S_ISREG(s.st_mode);
		if (file != 0) {
			fprintf(stderr, "ERROR: First argument must be a directory.\n");
			return EXIT_FAILURE;
		}
    /* Check file exists in project */
    char* p = (char*)malloc(strlen(argv[2]) + strlen(argv[3]) + 2);
    if (strstr(argv[3], argv[2]) == NULL || (strstr(argv[3], argv[2]) != argv[3])) {
      if (argv[2][strlen(argv[2]) - 1] != '/') {
				snprintf(p, strlen(argv[2]) + strlen(argv[3]) + 2, "%s/%s", argv[2], argv[3]);
			}
      else {
				snprintf(p, strlen(argv[2]) + strlen(argv[3]) + 2, "%s%s", argv[2], argv[3]);
			}
    }
    else {
      free(p);
			p = (char *) malloc(strlen(argv[3]) + 1);
			strcpy(p, argv[3]);
			p[strlen(argv[3])] = '\0';
    }
    int f = open(p, O_RDONLY);
    if (f < 0 && strcmp("add", argv[1]) == 0) {
      fprintf(stderr, "ERROR: File \"%s\" does not exist in project \"%s\".\n", argv[3], argv[2]);
			free(p);
			close(f);
			return EXIT_FAILURE;
    }
    /* Setup .Manifest file path */
    char *manifest_path = (char *) malloc(strlen(argv[2]) + 11);
    snprintf(manifest_path, strlen(argv[2]) + 11, "%s/.Manifest", argv[2]);
    int fd_mani = open(manifest_path, O_RDWR | O_CREAT, 0777);
    if (fd_mani < 0) {
			fprintf(stderr, "ERROR: Could not open or create .Manifest.\n");
			free(p);
			free(manifest_path);
			close(fd_mani);
			return EXIT_FAILURE;
		}
    char* t = (char*)malloc(sizeof(char) * INT_MAX);
    int len = read(fd_mani, t, INT_MAX);
    char* manifest_input = NULL;
    /* If local project existed before "create" was called, chances are it doesn't have initialized .Manifest yet */
    if (len != 0) {
      manifest_input = (char*)malloc(sizeof(char) * (len + 1));
      strcpy(manifest_input, t);
    }
    else {
      manifest_input = (char *) malloc(sizeof(char) * 3);
			manifest_input = "0\n";
			write(fd_mani, "0\n", 2);
    }
    free(t);
    if (strcmp(argv[1], "add") == 0) {
      /* Get input of file and hash it */
      int size = get_file_size(f);
      char input[size + 1];
      read(f, input, size);
      input[size] = '\0';
      unsigned char hash[SHA256_DIGEST_LENGTH];
      SHA256(input, strlen(input), hash); // This line causes compilation error
      char hashed[SHA256_DIGEST_LENGTH * 2 + 1];
      int i = 0;
			for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
				sprintf(hashed + (i * 2), "%02x", hash[i]);
			}
      /* Add to .Manifest */
			if (add(fd_mani, hashed, p, manifest_input, 0) == -1) {
				free(p);
				free(manifest_path);
				close(fd_mani);
				return EXIT_FAILURE;
			}
      printf("Addition of \"%s\" successful.\n", p);
			free(p);
			free(manifest_path);
			close(fd_mani);
    }
    else {
      /* No need for hash or to check if file exists, just get rid of it from .Manifest */
			if (removeFile(fd_mani, p, manifest_input) == -1) {
				free(p);
				free(manifest_path);
				close(fd_mani);
				return EXIT_FAILURE;
			}
			printf("Removal of \"%s\" successful.\n", p);
			free(p);
			free(manifest_path);
			close(fd_mani);
    }
  }
    else {
    /* Set up socket */
    int sockfd;
    struct addrinfo hints, *res, *p;
    char* token;
    int fd_conf = open("./.configure", O_RDONLY);
    /* Get info from .Configure, if it exists */
    if(fd_conf < 0) {
      error("ERROR: Could not open \".configure\" file");
    }
    char buff[50];
    read(fd_conf, buff, 50);
    token = strtok(buff, "\n");
    char* host = (char*) malloc(strlen(token) + 1);
    strcpy(host, token);
    token = strtok(NULL, "\n");
    char *port = (char *) malloc(strlen(token) + 1);
    strcpy(port, token);
    int received, sent;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
      error("ERROR: getaddrinfo() failed");
    }
    p = res;
    printf("Waiting for server...\n");
    while (1) {
      if (p == NULL) {
        p = res;
      }
      if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
        fprintf(stderr, "ERROR: Could not open client-side socket.\n");
        p = p->ai_next;
        sleep(3);
        continue;
      }
      if (connect(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
        close(sockfd);
        p = p->ai_next;
        sleep(3);
        continue;
      }
      break;
    }
    freeaddrinfo(res);
    if (strcmp(argv[1], "create") == 0) {
      /* CREATE */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      char sending[strlen(argv[2]) + 3];
      char buff[15];
      /* For each different command, send server single char (representing command) with project name */
      snprintf(sending, strlen(argv[2]) + 3, "c:%s", argv[2]);
      sent = send(sockfd, sending, strlen(sending), 0);
      received = recv(sockfd, buff, sizeof(buff) - 1, 0);
      buff[received] = '\0';
      /* Check for specific "error codes" sent from server */
			if (buff[0] == 'x') {
				fprintf(stderr, "ERROR: Project \"%s\" already exists on server.\n", argv[2]);
				return EXIT_FAILURE;
			}
      else {
        /* If directory exists, initialize, else issue error */
        struct stat s = {0};
        if (stat(argv[2], &s) == -1) {
          mkdir(argv[2], 0777);
          char* new_manifest_path = (char*)malloc(strlen(argv[2]) + 13);
          snprintf(new_manifest_path, strlen(argv[2]) + 13, "./%s/.Manifest", argv[2]);
          int file_mani = open(new_manifest_path, O_CREAT | O_WRONLY, 0777);
          write(file_mani, "0\n", 2);
          close(file_mani);
          free(new_manifest_path);
          printf("Project \"%s\" initialized successfully!\n", argv[2]);
        }
        else {
          fprintf(stderr, "ERROR: Project \"%s\" created on server, but directory of same name already exists on client-side.\n", argv[2]);
					return EXIT_FAILURE;
        }
      }
    }
    else if (strcmp(argv[1], "destroy") == 0) {
      /* DESTROY */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      char sending[strlen(argv[2]) + 3];
      snprintf(sending, strlen(argv[2]) + 3, "d:%s", argv[2]);
      sent = send(sockfd, sending, strlen(sending), 0);
      char buff[2];
      received = recv(sockfd, buff, sizeof(buff) - 1, 0);
      buff[received] = '\0';
      if (buff[0] == 'g') {
				printf("Project \"%s\" deleted on server successfully!\n", argv[2]);
			}
      else if (buff[0] == 'b') {
				fprintf(stderr, "ERROR: Failed to delete project \"%s\" on server.\n", argv[2]);
				return EXIT_FAILURE;
			}
      else {
        fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				return EXIT_FAILURE;
      }
    }
    else if (strcmp(argv[1], "currentversion") == 0) {
      /* CURRENTVERSION */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      char sending[strlen(argv[2]) + 3];
      snprintf(sending, strlen(argv[2]) + 3, "v:%s", argv[2]);
      sent = send(sockfd, sending, strlen(sending), 0);
      char buff[256];
      received = recv(sockfd, buff, 255, 0);
      if (buff[0] == 'x') {
				fprintf(stderr, "ERROR: Failed to get current version for project \"%s\" from server.\n", argv[2]);
				return EXIT_FAILURE;
			}
      buff[received] = '\0';
      /* For all files sent from server, get file size first, then get actual file contents */
			/* Getting .Manifest from server */
      int size = atoi(buff);
      char *v = (char*)malloc(size + 2);
      received = recv(sockfd, v, size, 0);
      if (v[0] == 'x') {
				fprintf(stderr, "ERROR: Failed to get current version for project \"%s\" from server.\n", argv[2]);
				return EXIT_FAILURE;
			}
      else if (v[0] == 'b') {
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				return EXIT_FAILURE;
			}
      if (iscntrl(v[0])) {
				++v;
			}
      if (received < size) {
        int offset = 0;
        int r = size - received;
        while ((received = recv(sockfd, (v + offset), r, 0)) > 0 && offset < size && r > 0) {
          printf("receiving: %s", v + offset);
					r -= received;
					offset += received;
        }
      }
      v[size] = '\0';
      token = strtok(v, "\n");
      printf("PROJECT: %s (Version %s)\n", argv[2], token);
			printf("----------------------------\n");
			int count = 1;
      /* Print file number and file path, but skip hash */
      while (token != NULL) {
        token = strtok(NULL, "\n\t");
        if (token == NULL && count == 1) {
          printf("No entries\n");
					break;
        }
        else if (token == NULL) {
          break;
        }
        if (count % 3 == 0) {
          ++count;
          continue;
        }
        if (count % 3 == 2) {
          printf("%s\n", token);
        }
        else if (count % 3 == 1) {
          printf("%s\t", token);
        }
        ++count;
      }
    }
    else if (strcmp(argv[1], "commit") == 0) {
      /* COMMIT */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      int send_size = strlen(argv[2] + 3);
      char* to_send = (char*)malloc(send_size);
      /* Check if .Update exists; if yes, check if it's empty */
      char update_path[strlen(argv[2]) + 9];
			snprintf(update_path, strlen(argv[2]) + 9, "%s/.Update", argv[2]);
			int update_fd = open(update_path, O_RDONLY);
      if (update_fd >= 0) {
				char temp[2];
				int bytes_read = read(update_fd, temp, 1);
				if (bytes_read > 0) {
					snprintf(to_send, send_size, "x");
					sent = send(sockfd, to_send, 2, 0);
					fprintf(stderr, "ERROR: Non-empty .Update exists locally for project \"%s\".\n", argv[2]);
					free(to_send);
					close(update_fd);
					return EXIT_FAILURE;
				}
				close(update_fd);
			}
      snprintf(to_send, send_size, "o:%s", argv[2]);
      sent = send(sockfd, to_send, send_size, 0);

      /* Get server's .Manifest */
      char* receiving = (char*)malloc(sizeof(int));
      received = recv(sockfd, receiving, sizeof(int), 0);
      if (receiving[0] == 'x') {
				fprintf(stderr, "ERROR: Failed to get server's .Manifest for project \"%s\" from server.\n", argv[2]);
				free(to_send);
				free(receiving);
				return EXIT_FAILURE;
			}
      else if (receiving[0] == 'b') {
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				free(to_send);
				free(receiving);
				return EXIT_FAILURE;
			}
      int server_manifest_size = atoi(receiving);
			free(receiving);

      receiving = (char *) malloc(server_manifest_size + 1);
      received = recv(sockfd, receiving, server_manifest_size, 0);
			while (received < server_manifest_size) {
				int bytes_received = recv(sockfd, receiving + received, server_manifest_size,0);
				received += bytes_received;
			}
      char server_manifest_input[received + 1];
			strcpy(server_manifest_input, receiving);
			server_manifest_input[received] = '\0';
			/* Get client's .Manifest */
			char* client_manifest = (char*)malloc(strlen(argv[2]) + 11);
			snprintf(client_manifest, strlen(argv[2]) + 11, "%s/.Manifest", argv[2]);
			int fd_mani = open(client_manifest, O_RDWR);
			int client_manifest_size = get_file_size(fd_mani);

      if (fd_mani < 0 || client_manifest_size < 0) {
				fprintf(stderr, "ERROR: Unable to open local .Manifest for project \"%s\".\n", argv[2]);
				free(to_send);
				to_send = (char*)malloc(2);
				snprintf(to_send, 2, "x");
				sent = send(sockfd, to_send, 2, 0);
				free(to_send);
				free(receiving);
				free(client_manifest);
				return EXIT_FAILURE;
			}
      char client_manifest_input[client_manifest_size + 1];
			int br = read(fd_mani, client_manifest_input, client_manifest_size);
			client_manifest_input[br] = '\0';

      /* Check versions; if they don't match, cease operation */
      char get_version[256];
      strcpy(get_version, client_manifest_input);

      char* vers_tok = strtok(get_version, "\n");

      int client_manifest_version = atoi(vers_tok);
			char stemp[strlen(server_manifest_input)];

      snprintf(stemp, strlen(server_manifest_input), "%s", server_manifest_input);
      char* server_vers_tok = strtok(stemp, "\n");

      if (client_manifest_version != atoi(server_vers_tok)) {
				fprintf(stderr, "ERROR: Local \"%s\" project has not been updated.\n", argv[2]);
				free(to_send);
				to_send = (char *) malloc(2);
				snprintf(to_send, 2, "x");
				sent = send(sockfd, to_send, 2, 0);
				free(to_send);
				free(receiving);
				free(client_manifest);
				return EXIT_FAILURE;
			}

      /* Setup .Commit */
      char* path_commit = (char*)malloc(strlen(argv[2]) + 10);
			snprintf(path_commit, strlen(argv[2]) + 10, "%s/.Commit", argv[2]);
			int commit_fd = open(path_commit, O_RDWR | O_CREAT | O_TRUNC, 0777);
      if (commit_fd < 0) {
        printf("ERROR: Could not open or create .Commit for project \"%s\".\n", argv[2]);
        free(to_send);
        to_send = (char*)malloc(2);
        snprintf(to_send, 2, "x");
        sent = send(sockfd, to_send, 2, 0);
        free(to_send);
        free(receiving);
        free(client_manifest);
        free(path_commit);
        close(commit_fd);
        return EXIT_FAILURE;
      }
      /* Run helper function to fill .Commit */
      if (commit(commit_fd, client_manifest_input, server_manifest_input, fd_mani) == -1) {
        fprintf(stderr, "ERROR: Local \"%s\" project is not up-to-date with server.\n", argv[2]);
        free(to_send);
        to_send = (char*)malloc(2);
        snprintf(to_send, 2, "b");
        sent = send(sockfd, to_send, 2, 0);
        free(to_send);
        free(receiving);
        free(client_manifest);
        return EXIT_FAILURE;
      }
      free(to_send);
      int commit_size = get_file_size(commit_fd);
      if (commit_size < 0) {
        fprintf(stderr, "ERROR: Could not get size of .Commit for \"%s\" project.\n", argv[2]);
        free(to_send);
        to_send = (char*)malloc(2);
        snprintf(to_send, 2, "x");
        sent = send(sockfd, to_send, 2, 0);
        free(to_send);
        free(receiving);
        free(client_manifest);
        free(path_commit);
        close(commit_fd);
        return EXIT_FAILURE;
      }
      send_size = sizeof(commit_size);
      to_send = (char*)malloc(send_size + 1);
      snprintf(to_send, send_size, "%d", commit_size);
      sent = send(sockfd, to_send, send_size, 0);
      /* If .Commit is empty, don't bother doing anything else */
      if (commit_size == 0) {
				fprintf(stderr, "ERROR: .Commit for project \"%s\" is empty.\n", argv[2]);
				free(to_send);
				free(receiving);
				free(client_manifest);
				free(path_commit);
				close(commit_fd);
				return EXIT_FAILURE;
			}
      lseek(commit_fd, 0, SEEK_SET);
      free(to_send);
      to_send = (char*)malloc(commit_size);
      int b_read = read(commit_fd, to_send, commit_size);
      sent = send(sockfd, to_send, commit_size, 0);
      received = recv(sockfd, receiving, 2, 0);
      /* Ensure server was able to create its own .Commit */
      if (receiving[0] == 'b') {
				fprintf(stderr, "ERROR: Server failed to create its own .Commit for project \"%s\".\n", argv[2]);
				free(receiving);
				free(to_send);
				return EXIT_FAILURE;
			}
      else if (receiving[0] == 'g') {
				free(receiving);
				free(to_send);
				printf("Commit successful!\n");
			}
      close(commit_fd);
    }
    // else if (strcmp(argv[1], "push") == 0) {
    //   /* PUSH */
    // }
  }
  return 0;
}
