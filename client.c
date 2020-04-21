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

  // step 1 create socket file descriptor
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

  // // close the socket
  // close(sockfd);

  return 0;
}
