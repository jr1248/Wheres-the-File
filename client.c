#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>


// method to print error msg
void error(char *msg){
  perror(msg);
  exit(0);
}



int main(int argc, char const *argv[]){
  char* IP;
  char* port;

  /*Configure comand*/
  //Step 1 try to open configure file

    int fd = open("./.configure", O_RDONLY);
    /*  if(fd < 0){
      printf("Need to configure\n");
      close(fd);
      return 0;
      } */ 
    // check first cmd for configure
    if(strcmp(argv[1],"configure") == 0){
     
      //make sure you have 4 arguments
       if(argc < 4){
	printf("Missing argurment");
       }
       else{ 
      //create configure and write in ip address and port number
	 fd=open("./.configure",O_WRONLY | O_CREAT, 0777);
	 strcpy(IP,argv[2]);
	 strcat(IP,"\n");
	 write(fd, IP, strlen(IP));
	 strcpy(port,argv[3]);
	 strcat(port,"\n");
	 write(fd, port, strlen(port));
	 printf("Configure success\n");
	 return 0;
       }
    }
    

  /*creating client down hear for now will change once methods are added*/

  // step 1 create socket file descriptor

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd < 0){
    printf("Could not open socket\n");
  }

  // step 2 get ip/host name
   struct hostent* client_name = gethostbyname(IP);
   if(client_name == NULL){
     printf("Host does not exist\n"); 
   }

  // step 3 build struct to connect

  struct sockaddr_in server_connection;

  // step 4 initialize connection

  bzero((char*)&server_connection, sizeof(server_connection));

  //step 5 make server_connection related to internet
  server_connection.sin_family = AF_INET;
  bcopy((char*)client_name->h_addr, (char*)&server_connection.sin_addr.s_addr, client_name->h_length);
  server_connection.sin_port = htons(atoi(port));
  


  //connect to server
  int status = connect(sockfd, (struct sockaddr*)&server_connection, sizeof(server_connection));

  if (status == -1) {
    error("Error making a connection to remote socket\n");
    sleep(3);
  }

  // recieve data from server
  char server_response[256];
  recv(sockfd, &server_response, sizeof(server_response), 0);

  printf("%s\n", server_response);

  // close the socket
  close(sockfd);

  return 0;
}
