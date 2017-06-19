/* Server Code
 * By Tyler Yun
 */

/* Server side - UDP Code		*/
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "networks.h"
#include "goBackN_Helper.h"
#include "cpe464.h"

#define MAXBUF 80

typedef enum State STATE;

enum State {
	START, DONE, FILENAME, SEND_DATA, CHECK_RESPONSE, CHECK_WINDOW_STATUS,
	HANDLE_CLOSED_WINDOW, WAIT_ON_ACK, TIMEOUT_ON_ACK,
	WAIT_ON_EOF_ACK, TIMEOUT_ON_EOF_ACK
};

void parseSetupData(uint8_t *buf, int *windowSize, int *bufferSize);
void processClient(int32_t server_skNum, uint8_t *buf, int32_t recvLen, Connection *client, int *windowSize, int *bufferSize);
STATE setup_sendFlag2(Connection *client, uint8_t *buf);
STATE filename_state(Connection *client, int32_t *dataFile);
STATE send_data(Connection *client, uint8_t *packet, int32_t *packetLen, int32_t dataFile,
	int bufferSize, WindowSeq *windowSeq, BufferData *bufData);
STATE check_response(Connection *client, WindowSeq *windowSeq);
STATE check_window_status(WindowSeq *windowSeq);
STATE handle_closed_window(Connection *client, WindowSeq *windowSeq, BufferData *bufData);
STATE wait_on_eof_ack(Connection *client);
STATE timeout_on_eof_ack(Connection *client, WindowSeq *windowSeq);
void printClientIP(struct sockaddr_in6 * client);
int checkArgs(int argc, char *argv[]);
void processServer(int server_skNum);

int main ( int argc, char *argv[]  )
{ 
	uint32_t server_skNum = 0;				
	//struct sockaddr_in6 client;	// Can be either IPv4 or 6
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);

	sendtoErr_init(atof(argv[1]), DROP_ON, FLIP_ON,
		DEBUG_ON, RSEED_ON);
		
	server_skNum = udpServerSetup(portNumber);

	processServer(server_skNum);

	close(server_skNum);
	return 0;
}

void parseSetupData(uint8_t *buf, int *windowSize, int *bufferSize) {
	memcpy(windowSize, buf, 4);
	memcpy(bufferSize, buf+4, 4);
}

void processServer(int server_skNum) {
	pid_t pid = 0;
	int status = 0;
	uint8_t buf[MAX_BUF];
	Connection client;
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	uint32_t recvLen = 0;
	int windowSize = 0, bufferSize = 0;

	// get new client connection, fork() child, parent processes wait
	while (1) {
		if (select_call(server_skNum, LONG_TIME, 0, SET_NULL)) {
			recvLen = recv_buf(buf, MAX_LEN, server_skNum, &client,
				&flag, &seqNum);
			
			if (flag == 1) {
				parseSetupData(buf, &windowSize, &bufferSize);
				if (recvLen != CRC_ERROR) {
					if ((pid = fork()) < 0) {
						perror("fork");
						exit(-1);
					}
					// child process
					else if (pid == 0) {
						processClient(server_skNum, buf, recvLen, &client, &windowSize, &bufferSize);
						exit(0);
					}
					// returns immediately if no child has exited
					while (waitpid(-1, &status, WNOHANG) > 0);
				}
				else {
					//printf("Received corrupt data packet, going to drop it.\n");
				}
			}
			else {
				printf("Not flag 1 received\n");
			}
		}
	}
}

void init_BufData(BufferData *bufData, int len) {
	int i;

	for (i=0; i<len; i++) {
		bufData[i].lastPacket = 0;
	}
}

void processClient(int32_t server_skNum, uint8_t *buf, int32_t recvLen, Connection *client,
	int *windowSize, int *bufferSize)
{
	STATE state = START;
	int32_t dataFile = 0;
	int32_t packetLen = 0;
	uint8_t packet[MAX_LEN];
	uint32_t seqNum = 1;
	BufferData bufData[(*windowSize)];
	WindowSeq windowSeq;
	windowSeq.currentSeq = seqNum;
	windowSeq.baseSeq = 1;
	windowSeq.maxSeq = windowSeq.baseSeq + *windowSize - 1;
	windowSeq.windowSize = *windowSize;
	windowSeq.packetSeq = seqNum;
	windowSeq.rej = 0;
	windowSeq.seqChanged = 0;
	windowSeq.lastPacket = -1;

	init_BufData(bufData, *windowSize);

	while (state != DONE) {
		switch (state) {
			case START:
				state = setup_sendFlag2(client, buf);
				break;
			case FILENAME:
				state = filename_state(client, &dataFile);
				break;
			case SEND_DATA:
				state = send_data(client, packet, &packetLen, dataFile, *bufferSize,
					&windowSeq, bufData);
				break;
			case CHECK_RESPONSE:
				state = check_response(client, &windowSeq);
				break;
			case CHECK_WINDOW_STATUS:
				state = check_window_status(&windowSeq);
				break;
			case HANDLE_CLOSED_WINDOW:
				state = handle_closed_window(client, &windowSeq, bufData);
				break;
			case WAIT_ON_EOF_ACK:
				state = wait_on_eof_ack(client);
				break;
			case TIMEOUT_ON_EOF_ACK:
				state = timeout_on_eof_ack(client, &windowSeq);
				break;
			case DONE:
				//printf("Exiting!\n");
				break;
			default:
				break;
		}
	}
}

STATE setup_sendFlag2(Connection *client, uint8_t *buf) {
	STATE returnValue;
	uint8_t response[1];

	if ((client->socketNum = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		perror("Filename, open client socket");
		exit(-1);
	}

	// send flag 2 response for successful setup
	send_buf(response, 0, client, 2, 0, buf);
	returnValue = FILENAME;

	return returnValue;
}

STATE filename_state(Connection *client, int32_t *dataFile) {
	int returnValue = DONE;
	uint8_t buf[MAX_BUF];
	uint8_t response[1];
	int fnameLen;
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	uint8_t fileName[100];
	int32_t dataLen = 0;

	fnameLen = MAX_FNAME_LEN + sizeof(Header);

	if (select_call(client->socketNum, LONG_TIME, 0, NOT_NULL) == 0) {
		printf("Timeout after 10 seconds, client lost\n");
		return DONE;
	}

	dataLen = recv_buf(buf, fnameLen, client->socketNum, client, &flag, &seqNum);

	if (dataLen == CRC_ERROR) {
		return FILENAME;
	}

	memcpy(fileName, buf, fnameLen);

	if (((*dataFile) = open((const char *)fileName, O_RDONLY)) < 0) {
		send_buf(response, 0, client, FNAME_BAD, 0, buf);
		returnValue = DONE;
	}
	else {
		send_buf(response, 0, client, FNAME_OK, 0, buf);
		returnValue = SEND_DATA;
	}

	return returnValue;
}

/* Process any RR and REJ before sending the EOF */
int process_RR_REJ(Connection *client, WindowSeq *windowSeq) {
	int boolStatus = 0;
	uint8_t flag = 0;
	uint32_t packetSeq = 0;
	uint32_t seqNum = 0;
	int32_t dataLen = 0;
	uint8_t buf[MAX_LEN];

	while (boolStatus == 0) {
		if (select_call(client->socketNum, SHORT_TIME, 0, NOT_NULL)) {
			dataLen = recv_buf(buf, sizeof(Header) + 4, client->socketNum, client, &flag, &packetSeq);

			if (dataLen != CRC_ERROR) {
				if (flag == READY_RECEIVE) {
					// check to see if the ready receive is the current Seq Num
					memcpy(&seqNum, buf, 4);
					//printf("Received the RR: %d\n", seqNum);

					if (seqNum == windowSeq->currentSeq) {
						boolStatus = 1;
						return 1;
					}
				}
				else if (flag == REJECT) {
					// send back flags starting from REJ number
					uint32_t rej = 0;

					memcpy(&rej, buf, 4);
					//printf("Process the REJ: %d\n", rej);
					windowSeq->baseSeq = rej;
					windowSeq->maxSeq = windowSeq->baseSeq + windowSeq->windowSize - 1;
					windowSeq->currentSeq = rej;
					// the reject variable tells you how much data in buffer to send
					windowSeq->rej = windowSeq->windowSize;

					return 10;
				}
			}
		}
		else {
			windowSeq->currentSeq = windowSeq->baseSeq;
			windowSeq->rej = windowSeq->windowSize;
			return 10;		
		}
	}

	return -1;
}

STATE send_data(Connection *client, uint8_t *packet, int32_t *packetLen, int32_t dataFile,
	int bufferSize, WindowSeq *windowSeq, BufferData *bufData) {

	uint8_t buf[MAX_BUF];
	int32_t readLen = 0;
	STATE returnValue = DONE;
	int i = 0;
	int bufLen = 0;

	memset(buf, 0, MAX_BUF);

 	//printf("---------------\n");

 	// if there is no reject received, then read to buf
 	// and save data to bufData
 	if (windowSeq->rej == 0) {
 		readLen = read(dataFile, buf, bufferSize);

 		i = windowSeq->currentSeq % windowSeq->windowSize;
		bufLen = readLen;

		memset(bufData[i].buf, 0, MAX_BUF);
	 	memcpy(bufData[i].buf, buf, bufferSize);
	 	memcpy(&(bufData[i].bufLen), &bufLen, 4);
	 	bufData[i].seqNum = windowSeq->currentSeq;
 	}
 	// if there is a reject received, then do not read to buf
 	// but set the buf to bufData
 	else if (windowSeq->rej > 0) {
 		if (windowSeq->lastPacket == windowSeq->currentSeq) {
 			windowSeq->rej = 0;
 		}
 		//printf("Sending buffered packets now\n");
 		i = windowSeq->currentSeq % windowSeq->windowSize;


 		memcpy(buf, bufData[i].buf, bufferSize);
		readLen = bufData[i].bufLen;
		windowSeq->rej -= 1;

		if (bufData[i].seqNum != windowSeq->currentSeq) {
			readLen = read(dataFile, buf, bufferSize);

	 		i = windowSeq->currentSeq % windowSeq->windowSize;
			bufLen = readLen;

		 	memcpy(bufData[i].buf, buf, bufferSize);
		 	memcpy(&(bufData[i].bufLen), &bufLen, 4);
		 	bufData[i].seqNum = windowSeq->currentSeq;
		}
		else {

			if (windowSeq->rej <= 0) {
				//printf("Sending unbuffered packets now\n");
				windowSeq->rej = 0;
			}
		}
 	}

	//printf("Data to send: %s\n", buf);
	//printf("ReadLen: %d\n", readLen);
	//printf("DATA PACKET SEQ #: %d\n", windowSeq->currentSeq);

	int eofCheck = 0;

	switch (readLen) {
		case -1:
			perror("send data, read error");
			returnValue = DONE;
			break;
		case 0:
			//printf("Processed an EOF\n");
			eofCheck = process_RR_REJ(client, windowSeq);
			if (eofCheck == 10) {
				return CHECK_WINDOW_STATUS;
			}
			(*packetLen) = send_buf(buf, 1, client, END_OF_FILE, 0, packet);
			returnValue = WAIT_ON_EOF_ACK;
			break;
		default:
			(*packetLen) = send_buf(buf, readLen, client, DATA, windowSeq->currentSeq, packet);
			if (readLen < bufferSize) {
				//printf("Processed the last data packet\n");
				windowSeq->lastPacket = windowSeq->currentSeq;
			}
			else {
				//printf("Processed a data packet\n");
			}
			windowSeq->currentSeq += 1;
			returnValue = CHECK_RESPONSE;
			break;
	}

	return returnValue;
}

STATE check_response(Connection *client, WindowSeq *windowSeq) {
	int returnValue = DONE;
	uint8_t buf[MAX_BUF];
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	int32_t dataLen = 0;

	//printf("PROCESSING FOR RR and REJ\n");
	if (select_call(client->socketNum, 0, 0, NOT_NULL)) {
		// process the response
		dataLen = recv_buf(buf, sizeof(Header)+4, client->socketNum, client, &flag, &seqNum);

		if (dataLen == CRC_ERROR) {
			return CHECK_WINDOW_STATUS;
		}

		// received a RR
		if (flag == READY_RECEIVE) {
			uint32_t readyRecv = 0;

			memcpy(&readyRecv, buf, 4);
			//printf("Processed RR: %d\n", readyRecv);
			windowSeq->seqChanged = readyRecv - windowSeq->baseSeq;
			windowSeq->baseSeq = readyRecv;
			windowSeq->maxSeq = readyRecv + windowSeq->windowSize - 1;

			returnValue = CHECK_WINDOW_STATUS;
		}
		// received a REJ
		else if (flag == REJECT) {
			// send back flags starting from REJ number
			uint32_t rej = 0;

			memcpy(&rej, buf, 4);
			//printf("Processed REJ: %d\n", rej);
			windowSeq->seqChanged = rej - windowSeq->baseSeq;
			windowSeq->baseSeq = rej;
			windowSeq->maxSeq = windowSeq->baseSeq + windowSeq->windowSize - 1;
			windowSeq->currentSeq = rej;
			// the reject variable tells you how much data in buffer to send
			windowSeq->rej = windowSeq->windowSize;

			returnValue = CHECK_WINDOW_STATUS;
		}
		// could be wrong beacuse the buffer size is different here
		else if (flag == EOF_ACK) {
			returnValue = DONE;
		}
	}
	else {
		// no response
		returnValue = CHECK_WINDOW_STATUS;
	}

	return returnValue;
}

STATE check_window_status(WindowSeq *windowSeq) {
	int returnValue = DONE;

	if (windowSeq->currentSeq > windowSeq->maxSeq) {
		returnValue = HANDLE_CLOSED_WINDOW;
	}
	else {
		returnValue = SEND_DATA;
	}

	return returnValue;
}

STATE handle_closed_window(Connection *client, WindowSeq *windowSeq, BufferData *bufData) {
	int returnValue = DONE;
	uint8_t buf[MAX_BUF];
	uint8_t packet[MAX_LEN];
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	int32_t dataLen = 0;
	static int retryCount = 0;
	int i = 0;
	uint32_t lowestIndex = 0;
	static int buffered = 0;

	//printf("IN CLOSED WINDOW STATE\n");
	if (select_call(client->socketNum, SHORT_TIME, 0, NOT_NULL)) {
		// an RR or REJ came back
		dataLen = recv_buf(buf, sizeof(Header)+4, client->socketNum, client, &flag, &seqNum);

		if(dataLen == CRC_ERROR) {
			return HANDLE_CLOSED_WINDOW;
		}

		// received a RR
		if (flag == READY_RECEIVE) {
			uint32_t readyRecv = 0;

			memcpy(&readyRecv, buf, 4);
			//printf("RR received: %d\n", readyRecv);
			windowSeq->seqChanged = readyRecv - windowSeq->baseSeq;
			windowSeq->baseSeq = readyRecv;
			windowSeq->maxSeq = windowSeq->baseSeq + windowSeq->windowSize - 1;

			if (buffered == 1) {
				windowSeq->maxSeq = windowSeq->currentSeq + windowSeq->windowSize - 1;
				windowSeq->rej = windowSeq->maxSeq - readyRecv + 1;
				windowSeq->currentSeq = readyRecv;
				windowSeq->baseSeq = readyRecv;
				windowSeq->maxSeq = windowSeq->baseSeq + windowSeq->windowSize - 1;
				//windowSeq->rej = windowSeq->windowSize;

				buffered = 0;
			}

			returnValue = CHECK_WINDOW_STATUS;
		}
		else if (flag == REJECT) {
			// send back flags starting from REJ number
			uint32_t rej = 0;

			memcpy(&rej, buf, 4);
			//printf("REJ received: %d\n", rej);
			windowSeq->seqChanged = rej - windowSeq->baseSeq;
			windowSeq->baseSeq = rej;
			windowSeq->maxSeq = windowSeq->baseSeq + windowSeq->windowSize - 1;
			windowSeq->currentSeq = rej;
			// the reject variable tells you how much data in buffer to send
			windowSeq->rej = windowSeq->windowSize;

			returnValue = CHECK_WINDOW_STATUS;
		}
		else if (flag == EOF_ACK) {
			returnValue = DONE;
		}
	}
	else {
		// timed out so send the lowest unacknowledged
		// packet for 10 more tries
		for (i = 0; i < windowSeq->windowSize; i++) {
			if (bufData[i].seqNum < bufData[lowestIndex].seqNum) {
				lowestIndex = i;
			}
		}
		windowSeq->currentSeq = bufData[lowestIndex].seqNum;
		send_buf(bufData[lowestIndex].buf, bufData[lowestIndex].bufLen, client, DATA, bufData[lowestIndex].seqNum, packet);
		//printf("Sending buffered: %s\n", bufData[lowestIndex].buf);
		windowSeq->packetSeq += 1;
		retryCount++;
		//printf("Retry Count: %d\n", retryCount);
		buffered = 1;
		returnValue = HANDLE_CLOSED_WINDOW;
	}

	if (retryCount > MAX_TRIES) {
		printf("Sent data %d times, no ACK, client is probably gone - so I'm terminating\n", MAX_TRIES);
		returnValue = DONE;
	}
	return returnValue;
}

STATE wait_on_eof_ack(Connection *client) {
	int returnValue = DONE;
	uint8_t packet[MAX_LEN];
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	int32_t dataLen = 0;
	static int retryCount = 0;

	if ((returnValue = processSelect(client, &retryCount, TIMEOUT_ON_EOF_ACK, WAIT_ON_EOF_ACK, DONE, SHORT_TIME)) == WAIT_ON_EOF_ACK) {
		// received a packet
		dataLen = recv_buf(packet, sizeof(Header)+1, client->socketNum, client, &flag, &seqNum);
		//printf("DataLen: %d\n", dataLen);

		if (dataLen == CRC_ERROR) {
			//printf("Dropped EOF_ACK packet\n");
			return WAIT_ON_EOF_ACK;
		}

		if (flag == EOF_ACK) {
			//printf("Received EOF ACK from rcopy, going to terminate\n");
			returnValue = DONE;
		}
		else {
			// must resend EOF
			//printf("Did not receive the correct EOF ACK. Going to keep waiting!\n");
			returnValue = TIMEOUT_ON_EOF_ACK;
		}
	}
	else if (returnValue == TIMEOUT_ON_EOF_ACK) {
		// timed out
		returnValue = TIMEOUT_ON_EOF_ACK;
	}
	else if (returnValue == DONE) {
		//printf("Tried to send the EOF packet 10 times but did not go through!\n");
	}

	return returnValue;
}

STATE timeout_on_eof_ack(Connection *client, WindowSeq *windowSeq) {
	uint8_t buf[MAX_BUF];
	uint8_t packet[MAX_LEN];

	send_buf(buf, 1, client, END_OF_FILE, 0, packet);
	windowSeq->packetSeq += 1;

	return WAIT_ON_EOF_ACK;
}

void printClientIP(struct sockaddr_in6 * client)
{
	char ipString[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, &client->sin6_addr, ipString, sizeof(ipString));
	printf("Client info - IP: %s Port: %d ", ipString, ntohs(client->sin6_port));
	
}

int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;
	
	if (argc == 2) {
		portNumber = 0;
	}
	else if (argc == 3) {
		portNumber = atoi(argv[2]);
	}
	else {
		fprintf(stderr, "Usage %s error-percent [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	return portNumber;
}


