#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080

void connection_handler(int); /* function prototype */

void error(char *msg) {
    perror(msg);
    exit(1);
}

void connection_handler (int sock) {
   int n;
   char buffer[256];

   bzero(buffer,256);
   n = read(sock,buffer,255);
   if (n < 0) error("ERROR reading from socket");
   printf("Here is the message: %s\n", buffer);
   n = write(sock,"I got your message", 18);
   if (n < 0) error("ERROR writing to socket");
}

int main(int argc, char *argv[]) {

     int sockfd, newsockfd, clilen, pid;
     struct sockaddr_in server, client;

     // if (argc < 2) {
     //     fprintf(stderr,"ERROR, no port provided\n");
     //     exit(1);
     // }

     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0)
        error("ERROR opening socket");

     bzero((char *) &server, sizeof(server));
     // portno = atoi(argv[1]);
     server.sin_family = AF_INET;
     server.sin_addr.s_addr = INADDR_ANY;
     server.sin_port = htons( PORT );

     if (bind(sockfd, (struct sockaddr *) &server, sizeof(server)) < 0)
        error("ERROR on binding");

     listen(sockfd, 5);
     clilen = sizeof(client);

     while (1) {
         newsockfd = accept(sockfd, (struct sockaddr *) &client, &clilen);
         if (newsockfd < 0)
             error("ERROR on accept");
         pid = fork();
         if (pid < 0)
             error("ERROR on fork");
         if (pid == 0)  {
             close(sockfd);
             connection_handler(newsockfd);
             exit(0);
         }
         else close(newsockfd);
     } /* end of while */
     return 0; /* we never get here */
}
