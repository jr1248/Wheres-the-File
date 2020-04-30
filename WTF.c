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
    else if (strcmp(argv[1], "push") == 0) {
      /* PUSH */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      int send_size = 3 + strlen(argv[2]);
      char* to_send = (char*)malloc(send_size);
      /* Check if .Update exists, is not empty, and contains an M code; if yes, fail*/
      char update_path[strlen(argv[2]) + 9];
      snprintf(update_path, strlen(argv[2]) + 9, "%s/.Update", argv[2]);
      int update_fd = open(update_path, O_RDONLY);
      if (update_fd >= 0) {
        char temp[2];
        int br = read(update_fd, temp, 1);
        if (br > 0) {
          lseek(update_fd, 0, 0);
          int size = get_file_size(update_fd);
          char update_input[size + 1];
          br = read(update_fd, update_input, size);
          update_input[br] = '\0';
          char* update_tok = strtok(update_input, "\t\n");
          while (update_tok != NULL) {
            if (strcmp(update_tok, "M") == 0) {
              snprintf(to_send, send_size, "x");
              sent = send(sockfd, to_send, 2, 0);
              fprintf(stderr, "ERROR: Non-empty .Update exists locally for project \"%s\".\n", argv[2]);
              free(to_send);
              close(update_fd);
              return EXIT_FAILURE;
            }
            update_tok = strtok(NULL, "\t\n");
          }
        }
        close(update_fd);
      }
      snprintf(to_send, send_size, "p:%s", argv[2]);
      sent = send(sockfd, to_send, send_size, 0);
      while (sent < send_size) {
				int bytes_sent  = send(sockfd, to_send + sent, send_size, 0);
				sent += bytes_sent;
			}
      char* receiving = (char *) malloc(2);
      received = recv(sockfd, receiving, 2, 0);
      if (receiving[0] == 'b') {
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				return EXIT_FAILURE;
			}
      /* Send .Commit to server */
      char commit_path[strlen(argv[2]) + 9];
      snprintf(commit_path, strlen(argv[2]) + 9, "%s/.Commit", argv[2]);
      int fd_commit = open(commit_path, O_RDONLY);
      if (fd_commit < 0) {
				fprintf(stderr, "ERROR: Failed to open local .Commit for \"%s\" project.\n", argv[2]);
				return EXIT_FAILURE;
			}
      int size = get_file_size(fd_commit);
      if (size == -1) {
				fprintf(stderr, "ERROR: Failed to get size of local .Commit for \"%s\" project.\n", argv[2]);
				return EXIT_FAILURE;
			}
      /* Reading .Commit's input */
      char commit_input[size + 1];
      int bytes_read = read(fd_commit, commit_input, size);
      commit_input[size] = '\0';
      free(to_send);
      send_size = sizeof(bytes_read);
      to_send = (char*)malloc(send_size);
      if (bytes_read == 0) {
				fprintf(stderr, "ERROR: Empty .Commit for project \"%s\".\n", argv[2]);
				return EXIT_FAILURE;
			}
      snprintf(to_send, send_size, "%d", bytes_read);
      sent = send(sockfd, to_send, send_size, 0);
      send_size = bytes_read;
      free(to_send);
      to_send = (char*)malloc(send_size);
      snprintf(to_send, send_size, "%s", commit_input);
      sent = send(sockfd, to_send, send_size, 0);
      received = recv(sockfd, receiving, 1, 0);
      /* Check whether server could find matching .Commit */
      if (receiving[0] == 'x') {
				fprintf(stderr, "ERROR: Server failed during .Commit lookup for project \"%s\".\n", argv[2]);
				remove(commit_path);
				return EXIT_FAILURE;
			}
      else if (receiving[0] == 'b') {
				fprintf(stderr, "ERROR: Matching .Commit could not be found on server's copy of \"%s\".\n", argv[2]);
				remove(commit_path);
				return EXIT_FAILURE;
			}
      received = recv(sockfd, receiving, 1, 0);
      if (receiving[0] == 'x') {
				fprintf(stderr, "ERROR: Server could not open its .Manifest for project \"%s\".\n", argv[2]);
			}
      else if (receiving[0] == 'g') {
				printf("Server initializing new version of project \"%s\".\n", argv[2]);
			}
      received = recv(sockfd, receiving, 2, 0);
      if (receiving[0] == 'b') {
				fprintf(stderr, "ERROR: Server could not instantiate new version of project \"%s\".\n", argv[2]);
				return EXIT_FAILURE;
			}
      /* Open .Manifest */
      char manifest_path[strlen(argv[2]) + 11];
      snprintf(manifest_path, strlen(argv[2]) + 11, "%s/.Manifest", argv[2]);
      int fd_mani = open(manifest_path, O_RDONLY);
      int manifest_size = get_file_size(fd_mani);
      if (fd_mani < 0 || manifest_size < 0) {
				free(to_send);
				to_send = (char*)malloc(2);
				snprintf(to_send, 2, "x");
				sent = send(sockfd, to_send, 2, 0);
			}
      char buff[manifest_size + 1];
      bytes_read = read(fd_mani, buff, manifest_size);
      /* Same process as server: create copy of current .Manifest and version with updated version number
			* Implement new .Manifest immediately, but if failure, return old .Manifest */
      char mani[manifest_size + 1];
      char* mani_buff = (char*)malloc(manifest_size + 1);
      strncpy(mani, buff, bytes_read);
      strncpy(mani_buff, buff, bytes_read);
      char* mani_tok = strtok(buff, "\n");
      int mani_vers = atoi(mani_tok);
      mani_buff += strlen(mani_tok) + 1;
      char* wr_man = malloc(strlen(mani_buff) + 2 + sizeof(mani_vers + 1));
      snprintf(wr_man, strlen(mani_buff) + 2 + sizeof(mani_vers + 1), "%d\n%s", mani_vers + 1, mani_buff);
      close(fd_mani);
      fd_mani = open(manifest_path, O_RDWR | O_TRUNC);
      write(fd_mani, wr_man, strlen(wr_man));
      /* Tokenize .Commit and make changes to local .Manifest */
      int count = 0;
      char* commit_token;
      int i = 0, j = 0;
      int last_sep = 0;
      int tok_len = 0;
      int len = strlen(commit_input);
      int delete_check = 0;
      char* p = NULL;
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
					}
					free(commit_token);
				}
        else if (count % 4 == 2) {
					free(commit_token);
				}
        else if (count % 4 == 3) {
          if (!delete_check) {
            free(to_send);
            p = (char *) malloc(strlen(commit_token) + 1);
            strncpy(p, commit_token, strlen(commit_token));
            int file = open(commit_token, O_RDONLY);
            int file_size = get_file_size(file);
            if (file < 0 || file_size < 0) {
              to_send = (char*)malloc(2);
							snprintf(to_send, 2, "x");
							sent = send(sockfd, to_send, 2, 0);
							close(fd_mani);
							fd_mani = open(manifest_path, O_WRONLY | O_TRUNC);
              /* Failure: Revert to old .Manifest */
              write(fd_mani, mani, manifest_size);
              close(fd_mani);
              remove(commit_path);
              close(file);
              free(p);
              free(commit_token);
              return EXIT_FAILURE;
            }
            send_size = sizeof(file_size);
            to_send = (char *) malloc(send_size);
            snprintf(to_send, send_size, "%d", file_size);
            sent = send(sockfd, to_send, send_size, 0);
            free(to_send);
            send_size = file_size;
            to_send = (char*)malloc(send_size);
            read(file, to_send, send_size);
            sent = send(sockfd, to_send, send_size, 0);
            while (sent < file_size) {
							int bytes_sent = send(sockfd, to_send + sent, send_size, 0);
							sent += bytes_sent;
						}
            received = recv(sockfd, receiving, 2, 0);
            if (receiving[0] == 'x') {
              fprintf(stderr, "ERROR: Server could not open new copy of \"%s\" in project \"%s\".\n", commit_token, argv[2]);
							close(fd_mani);
              fd_mani = open(manifest_path, O_WRONLY | O_TRUNC);
              write(fd_mani, mani, manifest_size);
							close(fd_mani);
							free(p);
							remove(commit_path);
							close(file);
							free(commit_token);
							return EXIT_FAILURE;
            }
            close(file);
            free(commit_token);
          }
        }
        else if (count % 4 == 0) {
          /* For loop finishes before looking at last hash */
          char hashed[strlen(commit_token) + 1];
          strcpy(hashed, commit_token);
          hashed[strlen(commit_token)] = '\0';
          if (!delete_check) {
            add(fd_mani, hashed, p, wr_man, 1);
            int new_size = get_file_size(fd_mani);
            free(wr_man);
            wr_man = (char*)malloc(new_size + 1);
            lseek(fd_mani, 0, 0);
            int br = read(fd_mani, wr_man, new_size);
            wr_man[br] = '\0';
          }
          else {
            delete_check = 0;
          }
          free(commit_token);
          free(p);
        }
      }
      if (tok_len > 0) {
        commit_token = (char*)malloc(tok_len + 1);
        for (i = 0; i < tok_len; ++i) {
					commit_token[i] = commit_input[last_sep + i];
				}
        commit_token[tok_len] = '\0';
        if (!delete_check) {
					add(fd_mani, commit_token, p, wr_man, 1);
      	}
        free(commit_token);
      }
      received = recv(sockfd, receiving, 2, 0);
      if (receiving[0] == 'g') {
				printf("Push succeeded!\n");
				remove(commit_path);
			}
      else {
        printf("Push failed.\n");
				remove(commit_path);
      }
    }
    else if (strcmp(argv[1], "upgrade") == 0) {
      /* UPGRADE */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      int send_size = 3 + strlen(argv[2]);
      char* to_send = (char*)malloc(send_size);
      snprintf(to_send, send_size, "g:%s", argv[2]);
      sent = send(sockfd, to_send, send_size, 0);
      char* rcvg = (char *) malloc(2);
      received = recv(sockfd, rcvg, 2, 0);
      if (rcvg[0] == 'b') {
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				return EXIT_FAILURE;
			}
      /* Open local .Update */
      char* update_path = (char*)malloc(strlen(argv[2]) + 9);
      snprintf(update_path, strlen(argv[2]) + 9, "%s/.Update", argv[2]);
      int update_fd = open(update_path, O_RDWR);
      int update_size = get_file_size(update_fd);
      if (update_fd < 0 || update_size < 0) {
				fprintf(stderr, "ERROR: Could not open or create .Update for project \"%s\". Please perform an update first.\n", argv[2]);
				free(to_send);
				to_send = (char*)malloc(2);
				snprintf(to_send, 2, "x");
				sent = send(sockfd, to_send, 2, 0);
        free(rcvg);
				free(update_path);
				close(update_fd);
				return EXIT_FAILURE;
			}
      if (update_size == 0) {
				printf(".Update is empty, project \"%s\" up-to-date.\n", argv[2]);
				free(to_send);
				to_send = (char*)malloc(2);
				snprintf(to_send, 2, "b");
				sent = send(sockfd, to_send, 2, 0);
				free(rcvg);
				free(update_path);
				free(to_send);
				close(update_fd);
			}
      free(to_send);
      /* Send .Update to server */
      send_size = sizeof(update_size);
      to_send = (char*) malloc(send_size);
      snprintf(to_send, send_size, "%d", update_size);
      sent = send(sockfd, to_send, send_size, 0);
			free(to_send);
      send_size = update_size;
      to_send = (char*)malloc(send_size + 1);
			int bytes_read = read(update_fd, to_send, send_size);
			to_send[bytes_read] = '\0';
      sent = send(sockfd, to_send, send_size, 0);
      while (sent < bytes_read) {
				int bytes_sent = send(sockfd, to_send + sent, send_size, 0);
				sent += bytes_sent;
			}
      char update_input[bytes_read + 1];
			strcpy(update_input, to_send);
			update_input[bytes_read] = '\0';
			/* Tokenize .Update data */
      int count = 0;
      char* update_tok;
      int i = 0, j = 0;
      int last_sep = 0;
      int tok_len = 0;
      int len = strlen(update_input);
      int delete_check = 0, modify_check = 0;
      char* fpath = NULL;
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
					} else if (update_tok[0] == 'M') {
						modify_check = 1;
					}
					free(update_tok);
        }
        else if (count % 4 == 2) {
					free(update_tok);
				}
        else if (count % 4 == 3) {
          if (delete_check == 1) {
						remove(update_tok);
					}
          else {
            free(rcvg);
						rcvg = (char*)malloc(sizeof(int));
						received = recv(sockfd, rcvg, sizeof(int), 0);

            /* For anything that was M code or A code, get file content's from server */
            if (rcvg[0] == 'x') {
							fprintf(stderr, "ERROR: Server could not send contents of \"%s\".\n", update_tok);
							free(fpath);
							free(update_tok);
							close(update_fd);
							return EXIT_FAILURE;
						}
            int fsize = atoi(rcvg);
						free(rcvg);

            rcvg = (char*)malloc(fsize + 1);
            received = recv(sockfd, rcvg, fsize,0);
						while (received < fsize) {
							int br = recv(sockfd, rcvg + received, fsize, 0);
							received += br;
						}
            rcvg[received] = '\0';
            create_dirs(update_tok, argv[2], 0);
            int file_fd;
            if (modify_check) {
							file_fd = open(update_tok, O_WRONLY | O_TRUNC);
						} else {
							file_fd = open(update_tok, O_WRONLY | O_CREAT, 0777);
						}
            if (file_fd < 0) {
							free(rcvg);
							snprintf(to_send, 1, "x");
							sent = send(sockfd, to_send, 1, 0);
							free(to_send);
							fprintf(stderr, "ERROR: Failed to open \"%s\" file in project \"%s\".\n", update_tok, argv[2]);
							free(update_tok);
							close(file_fd);
							close(update_fd);
						}
            write(file_fd, rcvg, strlen(rcvg));
						close(file_fd);
						snprintf(to_send, 2, "g");
						sent = send(sockfd, to_send, 1, 0);
						free(update_tok);
          }
        }
        else {
          free(update_tok);
        }
      }
      close(update_fd);
      remove(update_path);
			free(rcvg);
			rcvg = (char*)malloc(sizeof(int) + 1);
			received = recv(sockfd, rcvg, sizeof(int), 0);
			rcvg[received] = '\0';
      /* New .Manifest will be the same as server's, so just get server's .Manifest */
      if (rcvg[0] == 'x') {
				fprintf(stderr, "ERROR: Server unable to send .Manifest for project \"%s\".\n", argv[2]);
				free(rcvg);
				free(to_send);
				return EXIT_FAILURE;
			}
      int manifest_size = atoi(rcvg);
			free(rcvg);
      rcvg = (char*)malloc(manifest_size + 1);
      received = recv(sockfd, rcvg, manifest_size, 0);
			while (received < manifest_size) {
				int bytes_received = recv(sockfd, rcvg + received, manifest_size, 0);
				received += bytes_received;
			}
      /* Open local .Manifest */
      char manifest_path[strlen(argv[2]) + 11];
      snprintf(manifest_path, strlen(argv[2]) + 11, "%s/.Manifest", argv[2]);
      int mani_fd = open(manifest_path, O_WRONLY | O_TRUNC);
      if (mani_fd < 0) {
				free(rcvg);
				snprintf(to_send, 2, "x");
				sent = send(sockfd, to_send, 1, 0);
				free(to_send);
				fprintf(stderr, "ERROR: Unable to open .Manifest for project \"%s\".\n", argv[2]);
				close(mani_fd);
			}
      write(mani_fd, rcvg, manifest_size);
			close(mani_fd);
			snprintf(to_send, 2, "g");
			sent = send(sockfd, to_send, 1, 0);
			free(rcvg);
			free(to_send);
			printf("Upgrade successful!\n");
    }
    else if (strcmp(argv[1], "update") == 0) {
      /* UPDATE */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      int send_size = strlen(argv[2]) + 3;
      char* to_send = (char*)malloc(send_size);
      snprintf(to_send, send_size, "u:%s", argv[2]);
      sent = send(sockfd, to_send, send_size, 0);
      char* rcvg = (char*)malloc(sizeof(int));
      received = recv(sockfd, rcvg, sizeof(int), 0);
      /* Just need server's .Manifest to complete */
      if (rcvg[0] == 'x') {
				fprintf(stderr, "ERROR: Failed to get server's .Manifest for project \"%s\" from server.\n", argv[2]);
				free(to_send);
				free(rcvg);
				return EXIT_FAILURE;
			} else if (rcvg[0] == 'b') {
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				free(to_send);
				free(rcvg);
				return EXIT_FAILURE;
			}
      int serv_manifest_size = atoi(rcvg);
      free(rcvg);

      rcvg = (char*)malloc(serv_manifest_size + 1);
      received = recv(sockfd, rcvg, serv_manifest_size, 0);
      char serv_manifest_input[received + 1];
      strcpy(serv_manifest_input, rcvg);
			serv_manifest_input[received] = '\0';

      /* Get version of server's .Manifest */
      char server_temp[strlen(serv_manifest_input)];
      snprintf(server_temp, strlen(serv_manifest_input), "%s", serv_manifest_input);
			char* version_tok = strtok(server_temp, "\n");
			int sv = atoi(version_tok);

      /* Open local .Manifest */
      char* client_manifest = (char*)malloc(strlen(argv[2]) + 11);
      snprintf(client_manifest, strlen(argv[2]) + 11, "%s/.Manifest", argv[2]);
      int mani_fd = open(client_manifest, O_RDWR);
      int cm_size = get_file_size(mani_fd);
      if (mani_fd < 0 || cm_size < 0) {
				fprintf(stderr, "ERROR: Unable to open local .Manifest for project \"%s\".\n", argv[2]);
				free(to_send);
				free(rcvg);
				return EXIT_FAILURE;
			}
      char client_manifest_input[cm_size];
			int br = read(mani_fd, client_manifest_input, cm_size);
			client_manifest_input[br] = '\0';

      /* Get version of client's .Manifest */
      char ct[br + 1];
			strcpy(ct, client_manifest_input);
			ct[br] = '\0';
			version_tok = strtok(ct, "\n");
			int cv = atoi(version_tok);
			lseek(mani_fd, 0, 0);

      /* Set up .Update */
      char* update_path = (char*)malloc(strlen(argv[2]) + 9);
      snprintf(update_path, strlen(argv[2]) + 9, "%s/.Update", argv[2]);
      int update_fd = open(update_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
      if (update_fd < 0) {
				fprintf(stderr, "ERROR: Could not open or create .Update for project \"%s\".\n", argv[2]);
				free(to_send);
        free(rcvg);
				free(update_path);
				close(update_fd);
				return EXIT_FAILURE;
			}

      int update_check = update(update_fd, client_manifest_input, serv_manifest_input, cv, sv);
      if (update_check == -1) {
				free(to_send);
				remove(update_path);
				close(update_fd);
			} else if (update_check == 0) {
				fprintf(stderr, "ERROR: Take care of all conflicts for project \"%s\" before attempting to update.\n", argv[2]);
				free(to_send);
				free(rcvg);
				remove(update_path);
				close(update_fd);
			}
      else {
				if (get_file_size(update_fd) > 0) {
					printf(".Update created successfully for project \"%s\"!\n", argv[2]);
				} else {
					printf("Already up to date!\n");
				}
				free(rcvg);
				free(to_send);
				close(update_fd);
			}
    }
    else if (strcmp(argv[1], "rollback") == 0) {
      /* ROLLBACK */
      if (argc < 4) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name and desired version number.\n");
				return EXIT_FAILURE;
			}
			if (argc > 4) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name and desired version number.\n");
				return EXIT_FAILURE;
			}
      /* Send rollback code, project name, and version all in 1 go */
      int send_size = strlen(argv[2]) + strlen(argv[3]) + 4;
      char* to_send = (char*)malloc(send_size);
      snprintf(to_send, send_size, "r:%s:%s", argv[2], argv[3]);
      sent = send(sockfd, to_send, send_size, 0);
      char* rcvg = (char*)malloc(2);
      received = recv(sockfd, rcvg, 2, 0);
      if (rcvg[0] == 'b') {
				fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
			}
      else if (rcvg[0] == 'x') {
				fprintf(stderr, "ERROR: Something went wrong on server during rollback of project \"%s\".\n", argv[2]);
			}
      else if (rcvg[0] == 'v') {
				fprintf(stderr, "ERROR: Invalid version number inputted. The version number must be less than the current version of project \"%s\" on the server.\n", argv[2]);
			}
      else if (rcvg[0] == 'g') {
				printf("Rollback successful!\n");
			}
    }
    else if (strcmp(argv[1], "history") == 0) {
      /* HISTORY */
			if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      int send_size = 3 + strlen(argv[2]);
      char* to_send = (char*)malloc(send_size);
      snprintf(to_send, send_size, "h:%s", argv[2]);
      sent = send(sockfd, to_send, send_size, 0);
      char* rcvg = (char*)malloc(2);
      received = recv(sockfd, rcvg, 1, 0);
      if (rcvg[0] == 'b' || rcvg[0] == 'x') {
        if (rcvg[0] == 'b') {
					fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				}
        else {
          fprintf(stderr, "ERROR: Server could not send history of project \"%s\".\n", argv[2]);
        }
        free(to_send);
				free(rcvg);
				return EXIT_FAILURE;
      }
      /* Get .History file from server */
      free(rcvg);
      rcvg = (char*)malloc(sizeof(int) + 1);
      received = recv(sockfd, rcvg, sizeof(int), 0);
      rcvg[received] = '\0';
      int size = atoi(rcvg);
			free(rcvg);
      rcvg = (char*)malloc(size + 1);
      received = recv(sockfd, rcvg, size, 0);
      while (received < size) {
				int bytes_read = recv(sockfd, rcvg + received, size, 0);
				received += bytes_read;
			}
			rcvg[received] = '\0';
      /* Print history */
      printf("%s", rcvg);
			free(rcvg);
			free(to_send);
    }
    else if (strcmp(argv[1], "checkout") == 0) {
      /* ChECKOUT */
      if (argc < 3) {
				fprintf(stderr, "ERROR: Not enough arguments. Please input the project name.\n");
				return EXIT_FAILURE;
			}
			if (argc > 3) {
				fprintf(stderr, "ERROR: Too many arguments. Please input only the project name.\n");
				return EXIT_FAILURE;
			}
      int send_size = 3 + strlen(argv[2]);
      char* to_send = (char*)malloc(send_size);
      if (exists(argv[2]) != -1) {
				fprintf(stderr, "ERROR: Project \"%s\" already exists on client side.\n", argv[2]);
				snprintf(to_send, 2, "x");
				sent = send(sockfd, to_send, 2, 0);
				free(to_send);
				return EXIT_FAILURE;
			}
      snprintf(to_send, send_size, "k:%s", argv[2]);
      sent = send(sockfd, to_send, send_size, 0);
      char* rcvg = (char*)malloc(2);
      received = recv(sockfd, rcvg, 2, 0);
      if (rcvg[0] == 'b' || rcvg[0] == 'x') {
        if (rcvg[0] == 'b') {
					fprintf(stderr, "ERROR: Project \"%s\" does not exist on server.\n", argv[2]);
				} else {
					fprintf(stderr, "ERROR: Server could not send history of project \"%s\".\n", argv[2]);
				}
				free(to_send);
				free(rcvg);
				return EXIT_FAILURE;
      }
      mkdir(argv[2], 0777);
      char abs_path[PATH_MAX + 1];
      char* p = realpath(argv[2], abs_path);
      int len = strlen(abs_path);
      free(to_send);
      to_send = (char*)malloc(sizeof(int));
      snprintf(to_send, sizeof(int), "%d", len);
      sent = send(sockfd, to_send, sizeof(int), 0);
      free(to_send);
			to_send = (char*)malloc(len + 1);
      strcpy(to_send, abs_path);
      to_send[len] = '\0';
      sent = send(sockfd, to_send, len, 0);
      while (sent < len) {
				int bytes_sent = send(sockfd, to_send + sent, len, 0);
				sent += bytes_sent;
			}
      /* Getting server's .Manifest */
      free(rcvg);
      rcvg = (char*)malloc(sizeof(int));
      received = recv(sockfd, rcvg, sizeof(int), 0);
      printf("received %d: %s\n", received, rcvg);
      if (rcvg[0] == 'x') {
				remove_directory(abs_path);
				free(to_send);
				free(rcvg);
				fprintf(stderr, "ERROR: Could not receive server's copy of project \"%s\".\n", argv[2]);
				return EXIT_FAILURE;
			}
      /* Retrieving .Manifest */
      int manifest_size = atoi(rcvg);
      printf("manifest_size: %d\n", manifest_size);
      free(rcvg);
      rcvg = (char *) malloc(manifest_size + 1);
      received = recv(sockfd, rcvg, manifest_size, 0);
      while (received < manifest_size) {
				int br = recv(sockfd, rcvg + received, manifest_size, 0);
				received += br;
			}
      rcvg[received] = '\0';
      printf("yo: %s\n", rcvg);
      char manifest_path[strlen(argv[2])+ 11];
      snprintf(manifest_path, strlen(argv[2]) + 11, "%s/.Manifest", argv[2]);
      free(to_send);
			to_send = (char*)malloc(2);
      int mfd = open(manifest_path, O_CREAT | O_WRONLY, 0777);
      if (mfd < 0) {
				snprintf(to_send, 2, "x");
				sent = send(sockfd, to_send, 2, 0);
				free(to_send);
				free(rcvg);
				close(mfd);
				fprintf(stderr, "ERROR: Could not create local .Manifest for project \"%s\".\n", argv[2]);
				return EXIT_FAILURE;
			}
      write(mfd, rcvg, manifest_size);
			close(mfd);
      snprintf(to_send, 2, "g");
      sent = send(sockfd, to_send, 2, 0);
      free(to_send);
			free(rcvg);
      printf("Got project \"%s\" from server!\n", argv[2]);
    }
    else {
      /* If argv[1] didn't match any of the commands, send error code to server */
			char to_send[2] = "x";
			sent = send(sockfd, to_send, 2, 0);
    }
    close(sockfd);
  }
  return 0;
}
