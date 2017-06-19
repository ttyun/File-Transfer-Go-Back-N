#ifndef GOBACKN_HELPER_H
#define GOBACKN_HELPER_H

#define LONG_TIME 10
#define SHORT_TIME 1
#define CRC_ERROR -1
#define MAX_TRIES 10
#define FNAME 7
#define FNAME_BAD 9
#define FNAME_OK 8
#define READY_FOR_DATA 13
#define END_OF_FILE 10
#define DATA 3
#define READY_RECEIVE 5
#define REJECT 6
#define EOF_ACK 11

#include "networks.h"

typedef struct header {
	uint32_t seqNum;
	uint16_t checksum;
	uint8_t flag;
} Header;

typedef struct windowSeq {
	uint32_t baseSeq;
	uint32_t currentSeq;
	uint32_t maxSeq;
	int windowSize;
	uint32_t packetSeq;
	int rej;
	int seqChanged;
	uint32_t lastPacket;
} WindowSeq;

typedef struct bufferData {
	uint8_t buf[MAX_BUF];
	int bufLen;
	uint32_t seqNum;
	int lastPacket;
} BufferData;

int32_t send_buf(uint8_t *buf, uint32_t length, Connection *connection,
	uint8_t flag, uint32_t seqNum, uint8_t *packet);
int createHeader(uint32_t seqNum, uint8_t flag, uint32_t length, uint8_t *packet);
int32_t recv_buf(uint8_t *buf, int32_t len, int32_t socketNum,
	Connection *connection, uint8_t *flag, uint32_t *seqNum);
int retrieveHeader(char *dataBuf, int recvLen, uint8_t *flag, uint32_t *seqNum);
int processSelect(Connection *client, int *retryCount, int selectTimeoutState,
	int dataReadyState, int doneState, int timeout);

#endif