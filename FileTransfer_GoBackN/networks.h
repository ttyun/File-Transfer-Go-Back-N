
// 	Writen - HMS April 2017
//  Supports TCP and UDP - both client and server


#ifndef __NETWORKS_H__
#define __NETWORKS_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BACKLOG 10
#define MAX_LEN 1500
#define MAX_BUF 1400
#define MAX_FNAME_LEN 100
#define SET_NULL 0
#define NOT_NULL 1

typedef struct connection {
	uint32_t socketNum;
	struct sockaddr_in6 remote;
	uint32_t addrLen;
} Connection;

int32_t select_call(uint32_t socketNum, int32_t seconds, int32_t microsecs, int32_t set_null);
int32_t safeRecv(int32_t socketNum, char *dataBuf, int len, Connection *connection);
int32_t safeSend(uint8_t *packet, uint32_t length, Connection *connection);
//int safeRecv(int socketNum, void * buf, int len, int flags);
//int safeSend(int socketNum, void * buf, int len, int flags);
int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen);
int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen);

// for the server side
int tcpServerSetup(int portNumber);
int tcpAccept(int server_socket, int debugFlag);
int udpServerSetup(int portNumber);

// for the client side
int tcpClientSetup(char * serverName, char * port, int debugFlag);
int setupUdpClientToServer(Connection *server, char * hostName, int portNumber);
//int setupUdpClientToServer(Connection *connection, char * hostName, uint16_t portNumber);


#endif
