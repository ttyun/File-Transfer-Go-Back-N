
// Hugh Smith April 2017
// Network code to support TCP/UDP client and server connections

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "cpe464.h"
#include "networks.h"
#include "gethostbyname.h"

/* main select call that will return when data is ready */
int32_t select_call(uint32_t socketNum, int32_t seconds, int32_t microsecs, int32_t set_null) {
	fd_set fdSet;
	struct timeval aTimeout;
	struct timeval *timeout = NULL;

	if (set_null == NOT_NULL) {
		aTimeout.tv_sec = seconds;
		aTimeout.tv_usec = microsecs;
		timeout = &aTimeout;
	}

	FD_ZERO(&fdSet);
	FD_SET(socketNum, &fdSet);

	if (select(socketNum+1, (fd_set *)&fdSet, (fd_set *)0, (fd_set *)0, timeout) < 0) {
		perror("select");
		exit(-1);
	}

	if (FD_ISSET(socketNum, &fdSet)) {
		return 1;
	}
	else {
		return 0;
	}
}

int32_t safeSend(uint8_t *packet, uint32_t length, Connection *connection) {
	int sendLen = 0;

	//printf("Connection: %d\n", connection->remote.sin6_family);
	if ((sendLen = sendtoErr(connection->socketNum, packet, length, 0,
		(struct sockaddr *) &(connection->remote), connection->addrLen)) < 0) {

		perror("in send_buf(), sendto() call");
		exit(-1);
	}
	return sendLen;
}

/* recvfrom function used in recv_buf */
int32_t safeRecv(int32_t socketNum, char *dataBuf, int len, Connection *connection) {
	int recvLen = 0;
	uint32_t remoteLen = sizeof(struct sockaddr_in6);

	if ((recvLen = recvfrom(socketNum, dataBuf, len, 0, (struct sockaddr *) &(connection->remote),
		&remoteLen)) < 0) {

		perror("recv_buf, recvfrom");
		exit(-1);
	}
	//connection->addrLen = remoteLen;

	return recvLen;
}

int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen)
{
	int returnValue = 0;
	//int serverAddrLen = sizeof(struct sockaddr_in6);
	//printf("Addr Len: %d\n", *addrLen);
	if ((returnValue = recvfrom(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t *) addrLen)) < 0)
	{
		perror("recvfrom: ");
		exit(-1);
	}

	return returnValue;
}

int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen)
{
	int returnValue = 0;
	//printf("Addr Len: %d\n", addrLen);
	if ((returnValue = sendtoErr(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t) addrLen)) < 0)
	{
		perror("sendto: ");
		exit(-1);
	}
	
	return returnValue;
}

// This function sets the server socket. The function returns the server
// socket number and prints the port number to the screen.  

int tcpServerSetup(int portNumber)
{
	int server_socket= 0;
	struct sockaddr_in6 server;     
	socklen_t len= sizeof(server);  

	server_socket= socket(AF_INET6, SOCK_STREAM, 0);
	if(server_socket < 0)
	{
		perror("socket call");
		exit(1);
	}

	server.sin6_family= AF_INET6;         		
	server.sin6_addr = in6addr_any;   
	server.sin6_port= htons(portNumber);         

	// bind the name (address) to a port 
	if (bind(server_socket, (struct sockaddr *) &server, sizeof(server)) < 0)
	{
		perror("bind call");
		exit(-1);
	}
	
	// get the port name and print it out
	if (getsockname(server_socket, (struct sockaddr*)&server, &len) < 0)
	{
		perror("getsockname call");
		exit(-1);
	}

	if (listen(server_socket, BACKLOG) < 0)
	{
		perror("listen call");
		exit(-1);
	}
	
	printf("Server Port Number %d \n", ntohs(server.sin6_port));
	
	return server_socket;
}

// This function waits for a client to ask for services.  It returns
// the client socket number.   

int tcpAccept(int server_socket, int debugFlag)
{
	struct sockaddr_in6 clientInfo;   
	int clientInfoSize = sizeof(clientInfo);
	int client_socket= 0;

	if ((client_socket = accept(server_socket, (struct sockaddr*) &clientInfo, (socklen_t *) &clientInfoSize)) < 0)
	{
		perror("accept call");
		exit(-1);
	}
	  
	if (debugFlag)
	{
		printf("Client accepted.  Client IP: %s Client Port Number: %d\n",  
				getIPAddressString6(clientInfo.sin6_addr.s6_addr), ntohs(clientInfo.sin6_port));
	}
	

	return(client_socket);
}

int tcpClientSetup(char * serverName, char * port, int debugFlag)
{
	// This is used by the client to connect to a server using TCP
	
	int socket_num;
	uint8_t * ipAddress = NULL;
	struct sockaddr_in6 server;      
	
	// create the socket
	if ((socket_num = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
	{
		perror("socket call");
		exit(-1);
	}

	// setup the server structure
	server.sin6_family = AF_INET6;
	server.sin6_port = htons(atoi(port));
	
	// get the address of the server 
	if ((ipAddress = gethostbyname6(serverName, &server)) == NULL)
	{
		exit(-1);
	}

	if(connect(socket_num, (struct sockaddr*)&server, sizeof(server)) < 0)
	{
		perror("connect call");
		exit(-1);
	}

	if (debugFlag)
	{
		printf("Connected to %s IP: %s Port Number: %d\n", serverName, getIPAddressString6(ipAddress), atoi(port));
	}
	
	return socket_num;
}

int udpServerSetup(int portNumber)
{
	struct sockaddr_in6 server;
	int socketNum = 0;
	socklen_t serverAddrLen = 0;	
	
	// create the socket
	if ((socketNum = socket(AF_INET6,SOCK_DGRAM,0)) < 0)
	{
		perror("socket() call error");
		exit(-1);
	}
	
	// set up the socket
	server.sin6_family = AF_INET6;    		// internet (IPv6 or IPv4) family
	server.sin6_addr = in6addr_any ;  		// use any local IP address
	server.sin6_port = htons(portNumber);   // if 0 = os picks 

	// bind the name (address) to a port
	if (bind(socketNum,(struct sockaddr *) &server, sizeof(server)) < 0)
	{
		perror("bind() call error");
		exit(-1);
	}

	/* Get the port number */
	serverAddrLen = sizeof(server);
	getsockname(socketNum,(struct sockaddr *) &server, &serverAddrLen);
	printf("Server using Port #: %d\n", ntohs(server.sin6_port));

	return socketNum;	
	
}

int setupUdpClientToServer(Connection *server, char * hostName, int portNumber)
{
	// currently only setup for IPv4 
	int socketNum = 0;
	char ipString[INET6_ADDRSTRLEN];
	uint8_t * ipAddress = NULL;
	
	// create the socket
	if ((socketNum = socket(AF_INET6, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket() call error");
		exit(-1);
	}
  	 	
	if ((ipAddress = gethostbyname6(hostName, &(server->remote))) == NULL)
	{
		exit(-1);
	}
	
	server->remote.sin6_port = ntohs(portNumber);
	server->remote.sin6_family = AF_INET6;
	server->addrLen = sizeof(struct sockaddr_in6);
	
	inet_ntop(AF_INET6, ipAddress, ipString, sizeof(ipString));
	printf("Server info - IP: %s Port: %d \n", ipString, portNumber);
		
	return socketNum;
}
