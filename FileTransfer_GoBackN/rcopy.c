/* Rcopy Client Code
 * By Tyler Yun
 */

// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

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

#include "networks.h"
#include "goBackN_Helper.h"
#include "cpe464.h"

#define MAXBUF 80
#define xstr(a) str(a)
#define str(a) #a

typedef enum State STATE;

enum State {
	DONE, FILENAME, RECV_DATA, PROCESS_DATA, HANDLE_GOOD_DATA,
	HANDLE_BAD_DATA, END_CONNECTION, FILE_OK, START_STATE
};

// prototypes
uint16_t checkArgs(int argc, char * argv[]);
void processLogic(char **argv, Connection *server,
	int32_t *outputFileFd, uint16_t portNumber);
STATE start_state(char **argv, Connection *server, uint16_t portNumber);
STATE filename_state(char *fileName, Connection *server);
STATE file_ok(int *outputFileFd, char *outputFileName);
STATE ready_recv_data(Connection *server, int32_t *outputFileFd, int32_t *dataLen,
	uint8_t *buf, int bufferSize, uint8_t *flag, uint32_t *seqNum);
STATE process_data(uint8_t *buf, int32_t *dataLen, uint32_t *seqNum, uint32_t *RR, uint8_t *flag, uint32_t *expectedSeq);
STATE handle_good_data(Connection *server, uint8_t *buf, int dataLen, uint32_t *rcopySeqNum,
	uint32_t *RR, uint32_t *seqNum, uint32_t *expectedSeq, int outputFileFd);
STATE handle_bad_data(Connection *server, uint32_t *REJ, uint32_t *rcopySeqNum);
STATE end_connection(Connection *server);

int main (int argc, char *argv[])
 {
 	Connection server;
 	int32_t outputFileFd = 0;
 	uint16_t portNumber = 0;

 	portNumber = checkArgs(argc, argv);

 	// initialize the sendErr function with error percentage
 	// from command line argument
 	sendtoErr_init(atof(argv[5]), DROP_ON, FLIP_ON,
		DEBUG_ON, RSEED_ON);

 	processLogic(argv, &server, &outputFileFd, portNumber);

 	close(server.socketNum);
 	return 0;
}

/* main logical loop for rcopy to execute */
void processLogic(char **argv, Connection *server,
	int32_t *outputFileFd, uint16_t portNumber) {
	
	STATE state = START_STATE;
	uint32_t rcopySeqNum = 1;
	int32_t dataLen = 0;
	uint32_t seqNum = 0;
	uint8_t buf[MAX_BUF];
	uint32_t RR = 0, REJ = 0;
	int bufferSize = 0;
	uint8_t flag = 0;
	uint32_t expectedSeq = 0;

	bufferSize = atoi(argv[4]);

	while (state != DONE) {
		switch (state) {
			case START_STATE:
				state = start_state(argv, server, portNumber);
				break;
			case FILENAME:
				state = filename_state(argv[2], server);
				break;
			case FILE_OK:
				state = file_ok(outputFileFd, argv[1]);
				break;
			case RECV_DATA:
				state = ready_recv_data(server, outputFileFd, &dataLen, buf, bufferSize, &flag, &seqNum);
				break;
			case PROCESS_DATA:
				state = process_data(buf, &dataLen, &seqNum, &RR, &flag, &expectedSeq);
				break;
			case HANDLE_GOOD_DATA:
				state = handle_good_data(server, buf, dataLen, &rcopySeqNum, &RR, &seqNum, &expectedSeq, *outputFileFd);
				break;
			case HANDLE_BAD_DATA:
				REJ = seqNum;
				state = handle_bad_data(server, &REJ, &rcopySeqNum);
				break;
			case END_CONNECTION:
				state = end_connection(server);
			case DONE:
				break;
			default:
				printf("Not one of the specified states.\n");
				break;
		}
	}
}

STATE start_state(char **argv, Connection *server, uint16_t portNumber) {
	STATE returnValue = FILENAME;
	uint8_t buf[MAX_BUF];
	uint8_t packet[MAX_LEN];
	int len = 0;
	int32_t recvCheck = 0;
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	int windowSize, bufferSize;
	static int retryCount = 0;

	// if we have connected to server before, close it before reconnect
	if (server->socketNum > 0) {
		close(server->socketNum);
	}

	if ((server->socketNum = setupUdpClientToServer(server, argv[6], portNumber)) < 0) {
		// could not connect to server
		returnValue = DONE;
	}
	else {
		// send flag 1 and wait to receive flag 2
		// data for Flag 1 is window size and buffer size
		windowSize = atoi(argv[3]);
		bufferSize = atoi(argv[4]);
		memcpy(buf, &windowSize, 4);
		memcpy(buf+4, &bufferSize, 4);

		len = 8;
		send_buf(buf, len, server, 1, 0, packet);

		// wait to receive that the setup was successful
		if ((returnValue = processSelect(server, &retryCount, START_STATE, FILENAME, DONE, SHORT_TIME)) == FILENAME) {
			recvCheck = recv_buf(packet, MAX_LEN, server->socketNum, server, &flag, &seqNum);

			if (recvCheck == CRC_ERROR) {
				returnValue = START_STATE;
			}
			else if (flag != 2) {
				printf("Call setup response did not work.\n");
				returnValue = DONE;
			}
			else {
				//printf("Successful Setup.\n");
			}
		}
	}
	return returnValue;
}

STATE filename_state(char *fileName, Connection *server) {
	int returnValue = DONE;
	uint8_t packet[MAX_LEN];
	uint8_t buf[MAX_BUF];
	int fnameLen = 0;
	int recvCheck = 0;
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	static int retryCount = 0;

	// copy the filename into buf
	fnameLen = strlen(fileName) + 1;
	memcpy(buf, fileName, fnameLen);

	// send flag FNAME with packet seq Num of 0
	send_buf(buf, fnameLen, server, FNAME, 0, packet);

	if ((returnValue = processSelect(server, &retryCount, FILENAME, FILE_OK, DONE, SHORT_TIME)) == FILE_OK) {
		recvCheck = recv_buf(packet, MAX_LEN, server->socketNum, server, &flag, &seqNum);

		if (recvCheck == CRC_ERROR) {
			returnValue = FILENAME;
		}
		else if (flag == FNAME_BAD) {
			printf("File %s not found\n", fileName);
			returnValue = DONE;
		}
		else if (flag == FNAME_OK) {
			returnValue = FILE_OK;
		}
	}
	return returnValue;
}

STATE file_ok(int *outputFileFd, char *outputFileName) {
	STATE returnValue = DONE;

	if ((*outputFileFd = open(outputFileName, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0) {
		perror("File open error:");
		returnValue = DONE;
	}
	else {
		//printf("Successful file open %s\n", outputFileName);
		returnValue = RECV_DATA;
	}

	return returnValue;
}

STATE ready_recv_data(Connection *server, int32_t *outputFileFd, int32_t *dataLen,
	uint8_t *buf, int bufferSize, uint8_t *flag, uint32_t *seqNum) {

	int returnValue = PROCESS_DATA;

	//printf("----------\n");

	if (select_call(server->socketNum, LONG_TIME, 0, NOT_NULL) == 0) {
		perror("Timeout after 10 seconds, server must be gone\n");
		return DONE;
	}

	memset(buf, 0, MAX_BUF);

	*dataLen = recv_buf(buf, bufferSize+sizeof(Header), server->socketNum, server, flag, seqNum);
	//printf("Data Length: %d\n", *dataLen);

	if (*dataLen == CRC_ERROR) {
		return RECV_DATA;
	}

	if (*flag == DATA) {
		returnValue = PROCESS_DATA;
		//printf("Data Received: %s\n", buf);
	}
	else if (*flag == END_OF_FILE) {
		returnValue = END_CONNECTION;
	}

	return returnValue;
}

STATE process_data(uint8_t *buf, int32_t *dataLen, uint32_t *seqNum, uint32_t *RR, uint8_t *flag, uint32_t *expectedSeq) {
	int returnValue = DONE;
	static uint32_t expectedSeqNum = 1;

	//printf("Expected Sequence Number: %d\n", expectedSeqNum);
	//printf("Sequence Number Received: %d\n", *seqNum);

	if (*dataLen == CRC_ERROR) {
		returnValue = RECV_DATA;
	}
	if (*flag == END_OF_FILE) {
		returnValue = END_CONNECTION;
	}
	if (*seqNum > expectedSeqNum) {
		*seqNum = expectedSeqNum;
		returnValue = HANDLE_BAD_DATA;
	}
	else if (*seqNum < expectedSeqNum) {
		*RR = expectedSeqNum;
		returnValue = HANDLE_GOOD_DATA;
		*expectedSeq = expectedSeqNum;
	}
	else {
		if (*flag == DATA) {
			*expectedSeq = expectedSeqNum;
			expectedSeqNum++;
			*RR = expectedSeqNum;
			returnValue = HANDLE_GOOD_DATA;
		}
		else if (*flag == END_OF_FILE) {
			*expectedSeq = expectedSeqNum;
			expectedSeqNum++;
			returnValue = END_CONNECTION;
		}
	}

	return returnValue;
}

STATE handle_good_data(Connection *server, uint8_t *buf, int dataLen, uint32_t *rcopySeqNum,
	uint32_t *RR, uint32_t *seqNum, uint32_t *expectedSeq, int outputFileFd) {

	int returnValue = DONE;
	uint8_t packet[MAX_LEN];
	//uint32_t sendLen = 0;

	if (*seqNum == *expectedSeq) {
		write(outputFileFd, buf, dataLen);
	}

	memcpy(buf, RR, 4);
	//printf("RR sending: %d\n", *RR);

	send_buf(buf, 4, server, READY_RECEIVE, *rcopySeqNum, packet);
	//printf("Sent Length: (in bytes) %d\n", sendLen);
	(*rcopySeqNum)++;

	returnValue = RECV_DATA;

	return returnValue;
}

STATE handle_bad_data(Connection *server, uint32_t *REJ, uint32_t *rcopySeqNum) {
	int returnValue = DONE;
	uint8_t buf[MAX_BUF];
	uint8_t packet[MAX_LEN];

	memcpy(buf, REJ, 4);
	//printf("REJ sending: %d\n", *REJ);

	send_buf(buf, 4, server, REJECT, *rcopySeqNum, packet);
	(*rcopySeqNum)++;

	returnValue = RECV_DATA;
	return returnValue;
}

STATE end_connection(Connection *server) {
	uint8_t buf[MAX_BUF];
	uint8_t packet[MAX_LEN];
	//int32_t dataLen = 0;

	send_buf(buf, 1, server, EOF_ACK, 0, packet);
	//printf("DataLen: %d\n", dataLen);
	//printf("ENDING CONNECTION, File done\n");
	return DONE;
}

uint16_t checkArgs(int argc, char * argv[])
{
	// Checks args and returns port number
	int portNumber = 0;
	/* check command line arguments  */
	if (argc != 8)
	{
		printf("usage: %s local-TO-file remote-FROM-file window-size", argv[0]);
		printf(" buffer-size error-percent remote-machine remote-port\n");
		exit(1);
	}

	if (strlen(argv[1]) > MAX_FNAME_LEN) {
		printf("Local To File %s is too long. Must be less than 100 characters and is %d length\n",
			argv[1], (int)strlen(argv[1]));
	}

	if (strlen(argv[2]) > MAX_FNAME_LEN) {
		printf("Local To File %s is too long. Must be less than 100 characters and is %d length\n",
			argv[2], (int)strlen(argv[2]));
	}

	if (atoi(argv[4]) < 400 || atoi(argv[4]) > 1400) {
		printf("Buffer size needs to be between 400 and 1400 and is: %d\n", atoi(argv[4]));
		exit(-1);
	}

	if (atoi(argv[5]) < 0 || atoi(argv[5]) >= 1) {
		printf("Error rate needs to be between 0 and less than 1 and is %f\n", atof(argv[5]));
		exit(-1);
	}

	portNumber = atoi(argv[7]);
		
	return portNumber;
}





