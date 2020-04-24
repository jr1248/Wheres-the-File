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

int removeFile(int fd_mani, char *path, char *input);
int add(int fd_mani, char *hashcode, char *path, char *input, int flag);
unsigned int tokenize(char *path, char *input, char *hash, int flag, int *version);

char dashes[65] = "----------------------------------------------------------------";

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

/* Used by add and remove; will return locaton of matching file's hash or version number depending on flag */
unsigned int tokenize(char *path, char *input, char *hash, int flag, int *version) {
	if (input == NULL) {
		return 0;
	}
	char whole_string[strlen(input)];
	strcpy(whole_string, input);
	char *token;
	unsigned int byte_count = 0;
	unsigned int prev_bytes = 0;
	unsigned int check_bytes = 0;
	short check = 0;
	/* First token will always be the project version number, useless for now */
	token = strtok(whole_string, "\n\t");
	prev_bytes = strlen(token) + 1;
	byte_count += prev_bytes;
	int count = 0;

	while (token != NULL) {
		token = strtok(NULL, "\n\t");
		++count;
		if (token == NULL) {
			break;
		}
		if (count % 3 == 1 && version != NULL) {
			*version = atoi(token);
		}
		if (check == 1) {
			if (strcmp(token, hash) == 0 && !flag) {
				/* If the two hashes are equal, no need to put in new hash */
				return -1;
			} else {
				/* Will either return location of hash or location of file version number */
				return byte_count - (flag * check_bytes) - (flag * prev_bytes);
			}
		}
		if (strcmp(token, path) == 0) {
			/* If path found in .Manifest, check its hash, which will be next token */
			check = 1;
		}
		check_bytes = prev_bytes;
		prev_bytes = strlen(token) + 1;
		byte_count += prev_bytes;
	}
	return strlen(input);
}

/* Add a file to a .Manifest */
int add(int fd_mani, char *hashcode, char *path, char *input, int flag) {
	int *version = (int *) malloc(sizeof(int));
	*version = 0;
	/* Flag is 1 for operations that might increment file's version number, like push or upgrade */
	/* Else, for operations like add, will be 0 */
	int move = tokenize(path, input, hashcode, flag, version);
	if (move == 0) {
		fprintf(stderr, "ERROR: Could not read .Manifest.\n");
		return -1;
	}
	/* -1 means file's new hash matches hash already in .Manifest */
	if (move == -1 && !flag) {
		fprintf(stderr, "ERROR: File already up-to-date in .Manifest.\n");
		return -1;
	}

	lseek(fd_mani, move, SEEK_SET);
	/* If file wasn't in .Manifest, automatically will be added to the end with a starting version of 0 */
	if (move == strlen(input)) {
		write(fd_mani, "0\t", 2);
		write(fd_mani, path, strlen(path));
		write(fd_mani, "\t", 1);
		write(fd_mani, hashcode, strlen(hashcode));
		write(fd_mani, "\n", 1);
	} else if (!flag) {
		/* If it was in .Manifest and flag is not set, just update hash */
		write(fd_mani, hashcode, strlen(hashcode));
	} else {
		/* If it was and flag was set, increment file version number and update hash */
		int update = *version + 1;
		char update_string[sizeof(update) + 2];
		snprintf(update_string, sizeof(update) + 2, "%d\t", update);
		write(fd_mani, update_string, strlen(update_string));
		write(fd_mani, path, strlen(path));
		write(fd_mani, "\t", 1);
		write(fd_mani, hashcode, strlen(hashcode));
		write(fd_mani, "\n", 1);
	}
	return 0;
}

/* Remove a file from .Manifest by showing its hash code as a series of dashes */
int removeFile(int fd_mani, char *path, char *input) {
	/* Will never have to update version number of file being removed, so flag is always 0 */
	int move = tokenize(path, input, dashes, 0, NULL);
	/* Don't do anything if file isn't in .Manifest */
	if (move == strlen(input)) {
		fprintf(stderr, "ERROR: File \"%s\" not in .Manifest file.", path);
		return -1;
	}
	if (move == -1) {
		fprintf(stderr, "ERROR: File \"%s\" already removed from \".Manifest\" file.\n", path);
		return -1;
	}
	/* Write dashes in place of hash */
	lseek(fd_mani, move, SEEK_SET);
	write(fd_mani, dashes, strlen(dashes));
	return 0;
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
    //else if ()
  }
  return 0;
}
