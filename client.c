#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

/*define time out time for connection*/
#define timeout 3

/*define port number*/
#define portNumber 8080

// method to print error msg
/*void error(char *msg){
    perror(msg);
    exit(0);
}*/



int main(int argc, char const *argv[]){
	/*./WTF configure <IP/hostname> <port> command has 4 args most number
		we will have least number of args we will have is 3

	if (argc < 3){
		 printf("Usage:\t%s configure <IP Address> <Port>\n", first_arg);
		 return 0;
	}
	// create and connect client
	else{

	}*/

	//creating client down hear for now will change once methods are added

	// step 1 create socket file descriptor

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	// step 2 get ip/host name
	// struct hostent* client_name = gethostbyname("localhost");

	// step 3 build struct to connect

	struct sockaddr_in server_connection;

	// step 4 initialize connection

	// bzero((char*)&server_connection, sizeof(server_connection));

	//step 5 make server_connection related to internet
	server_connection.sin_family = AF_INET;
	server_connection.sin_port = htons(portNumber);
	server_connection.sin_addr.s_addr = INADDR_ANY;

	//step 6 connect to machine

	// bcopy((char*)client_name->h_addr, (char*)&server_connection.sin_addr.s_addr, client_name->h_length);

	//connect to server
	int status = connect(sockfd, (struct sockaddr*)&server_connection, sizeof(server_connection));

	if (status == -1) {
		printf("Error making a connection to remote socket\n");
	}

	// recieve data from server
	char server_response[256];
	recv(sockfd, &server_response, sizeof(server_response), 0);

	printf("%s\n", server_response);

	// close the socket
	close(sockfd);

	return 0;
}
