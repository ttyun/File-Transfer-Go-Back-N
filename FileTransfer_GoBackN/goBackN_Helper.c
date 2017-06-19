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
#include "goBackN_Helper.h"

int32_t send_buf(uint8_t *buf, uint32_t length, Connection *connection,
	uint8_t flag, uint32_t seqNum, uint8_t *packet);
int createHeader(uint32_t seqNum, uint8_t flag, uint32_t length, uint8_t *packet);
int32_t recv_buf(uint8_t *buf, int32_t len, int32_t socketNum,
	Connection *connection, uint8_t *flag, uint32_t *seqNum);
int retrieveHeader(char *dataBuf, int recvLen, uint8_t *flag, uint32_t *seqNum);
int processSelect(Connection *client, int *retryCount, int selectTimeoutState,
	int dataReadyState, int doneState, int timeout);


/* Takes care of creating the header and the buffer */
/* Sends the data packet */
int32_t send_buf(uint8_t *buf, uint32_t length, Connection *connection,
	uint8_t flag, uint32_t seqNum, uint8_t *packet) {

	int32_t sentLen = 0;
	int32_t sendingLen = 0;

	int addrLen = sizeof(connection->remote);

	// set up the packet (seq#, crc, flag, data)
	if (length > 0) {
		memcpy(&packet[sizeof(Header)], buf, length);
	}

	sendingLen = createHeader(seqNum, flag, length, packet);
	//sentLen = safeSend(packet, sendingLen, connection);
	sentLen = safeSendto(connection->socketNum, packet, sendingLen, 0, (struct sockaddr *)&(connection->remote), addrLen);

	return sentLen;
}

/* Creates the Header (puts in packet) */
/* including the seq num, checksum, and flag */
/* returns the packet size */
int createHeader(uint32_t seqNum, uint8_t flag, uint32_t length, uint8_t *packet) {
	Header *header = (Header *)packet;
	uint16_t checksum = 0;

	seqNum = htonl(seqNum);
	memcpy(&(header->seqNum), &seqNum, sizeof(seqNum));

	header->flag = flag;

	// Calculate checksum and put it into packet
	memset(&(header->checksum), 0, sizeof(checksum));
	checksum = in_cksum((unsigned short *)packet, length + sizeof(Header));
	memcpy(&(header->checksum), &checksum, sizeof(checksum));

	return (length + sizeof(Header));
}

/* main recv function to receive a packet and save sender's data */
/* returns the buf which is only the data and its length */
int32_t recv_buf(uint8_t *buf, int32_t len, int32_t socketNum,
	Connection *connection, uint8_t *flag, uint32_t *seqNum) {

	char dataBuf[MAX_LEN];
	int32_t recvLen = 0;
	int32_t dataLen = 0;

	int addrLen = sizeof(connection->remote);

	// recv packet
	//recvLen = safeRecv(socketNum, dataBuf, len, connection);
	recvLen = safeRecvfrom(socketNum, dataBuf, len, 0, (struct sockaddr *) &(connection->remote), &addrLen);

	// dataLen is the length of data, not including header
	dataLen = retrieveHeader(dataBuf, recvLen, flag, seqNum);

	// dataLen could be -1 if CRC error or 0 if no data
	if (dataLen > 0) {
		memcpy(buf, &(dataBuf[sizeof(Header)]), dataLen);
	}

	return dataLen;
}

/* check if packet is correct and parse through header data */
/* into parameters */
int retrieveHeader(char *dataBuf, int recvLen, uint8_t *flag, uint32_t *seqNum) {
	Header *header = (Header *)dataBuf;
	int returnValue = 0;

	if (in_cksum((unsigned short *)dataBuf, recvLen) != 0) {
		returnValue = CRC_ERROR;
	}
	else {
		*flag = header->flag;
		memcpy(seqNum, &(header->seqNum), sizeof(header->seqNum));
		*seqNum = ntohl(*seqNum);

		returnValue = recvLen - sizeof(Header);
	}
	return returnValue;
}

/* Processes a select mainly for server */
/* Waits for a select and handles situations */
/* if it times out or reaches max tries */
int processSelect(Connection *client, int *retryCount, int selectTimeoutState,
	int dataReadyState, int doneState, int timeout) {

	int returnValue = dataReadyState;

	(*retryCount)++;
	if (*retryCount > MAX_TRIES) {
		printf("Sent data %d times, no ACK, client is probably gone - so I'm terminating\n", MAX_TRIES);
		returnValue = doneState;
	}
	else {
		if (select_call(client->socketNum, timeout, 0, NOT_NULL)) {
			*retryCount = 0;
			returnValue = dataReadyState;
		}
		else {
			// select timed out
			returnValue = selectTimeoutState;
		}
	}
	return returnValue;
}


