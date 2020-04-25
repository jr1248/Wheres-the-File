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

int get_file_size(int fd) {
	struct stat st = {0};
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "ERROR: fstat() failed.\n");
		return -1;
	}
	return st.st_size;
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
    // else if (strcmp(argv[1], "currentversion") == 0) {
    //   /* CURRENTVERSION */
    // }
  }
  return 0;
}
