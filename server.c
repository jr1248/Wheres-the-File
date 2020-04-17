#include <stdio.h>
#include <stdlib.h>
#include<string.h>
#include<unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include<pthread.h> // threading

#define PORT 8080

void *connection_handler(void *);

// Handles the connection for each client
void *connection_handler(void *server_socket) {
  // Get the socket descriptor
  int socket = *(int*)server_socket;
  int read_size;
  char* message, client_message[2000];

  // Send some messages to the client
  message = "This is a message from the server. You have reached the server!\n";
  write(socket, message, strlen(message));

  //Receive a message from client
  while ( (read_size = recv(socket, client_message, 2000, 0)) > 0 ) {
    client_message[read_size] = '\0';
    write(socket, client_message, strlen(client_message));

    // clear message buffer
    memset(client_message, 0, 2000);
  }

  if (read_size == 0) {
    printf("Client disconnected\n");
    fflush(stdout);
  }
  else if (read_size == -1) {
    perror("recieve failed");
  }

  return 0;
}

int main(int argc, char* argv[]) {

  int server_socket, client_socket;
  struct sockaddr_in server, client;

  // create the server socket
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
    printf("Could not create socket\n");

  printf("Socket Created\n");

  // define the server address
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons( PORT );

  //Bind socket to our specified IP and sin_port
  if( bind(server_socket, (struct sockaddr *)&server , sizeof(server)) < 0) {
    perror("Bind failed. Error");
    return 1;
  }
  printf("Bind successful\n");

  // Listen
  listen(server_socket, 3);

  //Accept and incoming connection
  printf("Waiting for incoming connections...\n");
  int c = sizeof(struct sockaddr_in);
  pthread_t thread_id;

  while((client_socket = accept(server_socket, (struct sockaddr *)&client, (socklen_t*)&c))) {
    printf("Connection accepted\n");

    if (pthread_create(&thread_id, NULL, connection_handler, (void*) &client_socket) == -1) {
      perror("Could not create thread");
      return 1;
    }

    //Now join the thread , so that we dont terminate before the thread
    // pthread_join(thread_id , NULL);
  }

  if (client_socket == -1) {
    perror("Accept failed");
    return 1;
  }

  return 0;
}
