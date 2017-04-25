#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#define MAX 1024
#define MAXSEQ 1000000000
#define TRUE 1
#define FALSE 0
#define MAXPAYLOAD 996
#define MAXPACKET 5
#define DEFAULTTIMEOUT 7000

char DAT[] = "CSC361DAT ";
char SYN[] = "CSC361SYN ";
char FIN[] = "CSC361FIN ";
char RST[] = "CSC361RST \n\n";
const char *eventType[4] = {"s", "S", "r", "R"};
const char *packetType[5] = {"DAT", "ACK", "SYN", "FIN", "RST"};
int usedWindow = 0;
int seqNum = 0;
int ackNum = 0;
int payload = 0;
int winSize = 0;
int lastAck = 0;
int selectTimeout = 0;
int resendAckNum = 0;

int totalPacket = 0;
int uniquePacket = 0;
int totalData = 0;
int uniqueData = 0;
int numSynSend = 0;
int numFinSend = 0;
int numRstSend = 0;
int numAckRecv = 0;
int numRstRecv = 0;

int packetSize[MAXPACKET];
char unAckData[MAXPACKET][MAX];
int unAckSeqNum[MAXPACKET];
int expectedAckNum[MAXPACKET];
int unAckOldestIndex = 0;
int nextFreeUnAckIndex = 0;

struct tm *getTime;
struct timeval timer;
struct timeval current;
struct timeval start;
struct timeval currTime;
struct timeval sentTime[MAXPACKET];
double oneRTT = 0;

int sock;
struct sockaddr_in rcv;
struct sockaddr_in snd;
fd_set readfds;

void openSocket(){
	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if(sock == -1){
		perror("Failed to open a socket");
		exit(EXIT_FAILURE);
	}
}

void reusePort(){
	int opt = 1;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) == -1){
		perror("setsockopt failed");
		close(sock);
		exit(EXIT_FAILURE);
	}
}

//store source and destination IP address and port 
void initSockaddr(char* sIP, char* sPort, char* rIP, char* rPort){
	memset(&rcv, 0, sizeof(rcv));
	rcv.sin_family = AF_INET;
	rcv.sin_addr.s_addr = inet_addr(rIP);
	rcv.sin_port = htons(atoi(rPort)); 
	memset(&snd, 0, sizeof(snd));
	snd.sin_family = AF_INET;
	snd.sin_addr.s_addr = inet_addr(sIP);
	snd.sin_port = htons(atoi(sPort));
	
}

void bindAddr(){
	if(bind(sock, (struct sockaddr *) &snd, sizeof(snd)) == -1){
		perror("Failed to bind");
		close(sock);
		exit(EXIT_FAILURE);
	}
}

void sendToReceiver(const char *buffer, size_t size){
	if(sendto(sock, buffer, size, 0, (struct sockaddr*)&rcv, sizeof rcv) == -1){
		perror("sendto failed");
		close(sock);
		exit(EXIT_FAILURE);
	}
}

void senderLog(int event, int packet, int num, int pay){
	char buffer[10];
	memset(&buffer, 0, sizeof buffer);
	gettimeofday(&current, NULL);
	getTime = localtime(&current.tv_sec);
	strftime(buffer, 10, "%H:%M:%S", getTime);
	printf("%s.%d %s ", buffer, (int)current.tv_usec, eventType[event]);
	printf("%s:%d ", inet_ntoa(snd.sin_addr), ntohs(snd.sin_port));
	printf("%s:%d ", inet_ntoa(rcv.sin_addr), ntohs(rcv.sin_port));
	printf("%s ", packetType[packet]);
	if(packet != 4){
		printf("%d ", num);
		printf("%d\n", pay);
	}else{
		printf("\n");
	}
}

//get the time different in seconds
double timeDiff(struct timeval startTime, struct timeval endTime){
	double s, e, d;
	s = (double)startTime.tv_sec + (double)startTime.tv_usec/1000000.0;
	e = (double)endTime.tv_sec + (double)endTime.tv_usec/1000000.0;
	d = (double)e - (double)s;
	return d;
}

void summary(){
	printf("total data bytes sent: %d\n", totalData);
	printf("unique data bytes sent: %d\n", uniqueData);
	printf("total data packets sent: %d\n", totalPacket);
	printf("unique data packets sent: %d\n", uniquePacket);
	printf("SYN packets sent: %d\n", numSynSend);
	printf("FIN packets sent: %d\n", numFinSend);
	printf("RST packets sent: %d\n", numRstSend);
	printf("ACK packets received: %d\n", numAckRecv);
	printf("RST packet received: %d\n", numRstRecv);
	printf("total time duration (second): %f\n", timeDiff(start, current));
}

//remove packets from the buffer after received a cumulative ack
void removeFromBuff(){
	int i;
	for(i = 0; i < MAXPACKET; i++){
		if(expectedAckNum[i] != -1 && expectedAckNum[i] <= ackNum){
			memset(&unAckData[i], 0, sizeof(unAckData[i]));
			unAckSeqNum[i] = -1;
			expectedAckNum[i] = -1;
			packetSize[i] = 0;
			unAckOldestIndex = (unAckOldestIndex + 1)%MAXPACKET;
			usedWindow--;
		}
	}
}

//receive packets from the receiver
void receiveACK(){
	socklen_t fromlen = sizeof rcv;
	char b[MAX];
	memset(&b, 0, sizeof b);
	lastAck = ackNum;
	if(recvfrom(sock, b, sizeof b, 0, (struct sockaddr*)&rcv, &fromlen) == -1){
		perror("recvfrom failed");
		close(sock);
		exit(EXIT_FAILURE);
	}
	char *token = strtok(b, " ");
	//check to see if it's an ack packet or rst packet
	if(strcmp(token, "CSC361ACK") == 0){
		ackNum = atoi(strtok(NULL, " "));
		winSize = atoi(strtok(NULL, " "));
		removeFromBuff();
		numAckRecv++;
		if(lastAck == ackNum){
			//receive a duplicate ack number
			senderLog(3,1, ackNum, winSize);
		}else{
			//receive new ack number
			senderLog(2,1, ackNum, winSize);
		}
	}else if(strcmp(token, "CSC361RST") == 0){
		//rst packet received, print the summary and close connection
		senderLog(2, 4,0,0);
		numRstRecv++;
		summary();
		close(sock);
		exit(EXIT_FAILURE);
	}else{
		printf("Unknown packet received, not part of this connection\n");
	}
}

//resend the oldest unacknowledged packet
void resendPack(){
	sendToReceiver(unAckData[unAckOldestIndex], packetSize[unAckOldestIndex]);
	char temp[MAX];
	memset(&temp, 0, sizeof temp);
	strncpy(temp, unAckData[unAckOldestIndex], strlen(unAckData[unAckOldestIndex]));
	char *token = strtok(temp, " ");
	int p = (expectedAckNum[unAckOldestIndex] - unAckSeqNum[unAckOldestIndex]);
	resendAckNum = expectedAckNum[unAckOldestIndex];
	totalData += p;
	//print the log
	if(strcmp(token, "CSC361FIN") == 0){
		numFinSend++;
		senderLog(1, 3, unAckSeqNum[unAckOldestIndex], 0);
	}else if(strcmp(token, "CSC361DAT") == 0){
		senderLog(1, 0, unAckSeqNum[unAckOldestIndex], p);
		totalPacket++;
	}
	//set new time stamp for the resend packet
	sentTime[unAckOldestIndex].tv_sec = current.tv_sec;
	sentTime[unAckOldestIndex].tv_usec = current.tv_usec;
}

//send syn packet: "CSC361SYN initialSeqNum \n\n"
void sendSyn(){
	char buffer[MAX];
	char b[MAX];
	memset(&buffer, 0, sizeof buffer);
	memset(&b, 0, sizeof b);
	strncat(buffer, SYN, sizeof(SYN));
	int n = sprintf(b, "%d", seqNum);
	strncat(buffer, b, n);
	strcat(buffer, " \n\n");
	sendToReceiver(buffer, strlen(buffer));
	numSynSend++;
	senderLog(0, 2, seqNum, 0);
	start.tv_sec = current.tv_sec;
	start.tv_usec = current.tv_usec;
	//send syn packet up to 3 times 
	while (numSynSend < 5){
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		timer.tv_sec = 2;
		timer.tv_usec = 0;
		int selectReturn = select(sock+1, &readfds, NULL, NULL, &timer);
		if(selectReturn == -1){
			perror("select() failed");
			close(sock);
			exit(EXIT_FAILURE);
		}
		if(selectReturn == 0){
			//resend syn packet
			sendToReceiver(buffer, strlen(buffer));	
			senderLog(1, 2, seqNum, 0);
		}else if(FD_ISSET(sock, &readfds)){
			//receive something from the receiver
			receiveACK();
			break;
		}
		numSynSend++;
	}
	if(numSynSend == 5){
		//no response from the receiver after 3 syn packets, close connection 
		printf("No response timeout\n");
		close(sock);
		exit(EXIT_FAILURE);
	}
	//set sequence number for the next packet
	seqNum = (seqNum + 1) % MAXSEQ;
	//set one round trip time for timer
	oneRTT = timeDiff(start, current); 
}

//send rst packet: "CSC361RST \n\n"
void sendRST(){
	sendToReceiver(RST, strlen(RST));
	senderLog(0, 4, 0, 0);
	numRstSend++;
	summary();
	close(sock);
	exit(EXIT_FAILURE);
}

void receiveFromReceiver(){
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	int selectReturn = select(sock+1, &readfds, NULL, NULL, &timer);
	if(selectReturn == -1){
		perror("select() failed");
		close(sock);
		exit(EXIT_FAILURE);
	}
	if(selectReturn == 0){
		//select timeout, nothing receive
		if(selectTimeout < 5){
			//resend packet
			resendPack();
		}else{
			//select timeout more than 5 times, send rst packet
			sendRST();
		}
		selectTimeout++;
	}else if(FD_ISSET(sock, &readfds)){
		//receive something from the receiver
		receiveACK();
		selectTimeout = 0;
	}
	timer.tv_sec = 0;
	timer.tv_usec = DEFAULTTIMEOUT;
}

//send fin packet: "CSC361FIN seqNum \n\n"
void sendFIN(){
	//store FIN packet in the buffer
	char buffer[1024];
	memset(&buffer, 0, sizeof buffer);
	strcat(buffer, FIN);
	packetSize[nextFreeUnAckIndex] += strlen(FIN);
	char temp[10];
	memset(&temp, 0, sizeof temp);
	int n = sprintf(temp, "%d", seqNum);
	strncat(buffer, temp, n);
	strcat(buffer, " \n\n");
	packetSize[nextFreeUnAckIndex] += (n+3);
	strncpy(unAckData[nextFreeUnAckIndex], buffer, strlen(buffer));
	unAckSeqNum[nextFreeUnAckIndex] = seqNum;
	expectedAckNum[nextFreeUnAckIndex] = (seqNum + 1)%MAXSEQ;
	//send fin packet to receiver
	sendToReceiver(buffer, strlen(buffer));
	senderLog(0, 3, seqNum, 0);
	//assign time stamp for fin packet
	sentTime[nextFreeUnAckIndex].tv_sec = current.tv_sec;
	sentTime[nextFreeUnAckIndex].tv_usec = current.tv_usec;
	nextFreeUnAckIndex = (nextFreeUnAckIndex + 1)%MAXPACKET;
	numFinSend++;
	while(ackNum != (seqNum+1)){
		timer.tv_sec = 0;
		timer.tv_usec = DEFAULTTIMEOUT;
		receiveFromReceiver();
	}
}

int isTimeout(){
	//check if the oldest unacknowledged packet timeout, if so resend the oldest unacknowledged packet
	gettimeofday(&currTime, NULL);
	if(unAckSeqNum[unAckOldestIndex] != -1 && timeDiff(sentTime[unAckOldestIndex], currTime) > 6*oneRTT){
		resendPack();
		totalData += (expectedAckNum[unAckOldestIndex] - unAckSeqNum[unAckOldestIndex]);
		totalPacket++;
		return TRUE;
	}
	return FALSE;
}

//send dat packet
void sendData(char *fileName){
	FILE *f;
	f = fopen(fileName, "rb");
	if(f == NULL){
		printf("Cannot open file: %s\n", fileName);
		close(sock);
		exit(EXIT_FAILURE);
	}
	char buffer[MAX];
	char temp[10];
	char readBuffer[1000];
	int r = 0;
	while(TRUE){
		timer.tv_sec = 0;
		timer.tv_usec = DEFAULTTIMEOUT;
		if(isTimeout() == TRUE){
			receiveFromReceiver();
			while(resendAckNum != ackNum){
				receiveFromReceiver();				
			}
		}
		else if(usedWindow < MAXPACKET){
			//free space in the buffer, send data packet
			memset(&readBuffer, 0, sizeof(readBuffer));
			memset(&temp, 0, sizeof(temp));
			memset(&buffer, 0, sizeof(buffer));
			r = fread(readBuffer, sizeof(char), MAXPAYLOAD, f);
			payload = r;
			uniqueData += payload;
			totalData += payload;
			if(r <= 0){
				//no more data read from file
				break;
			}
			//store header and data in a buffer 
			strncat(buffer, DAT, sizeof(DAT));
			packetSize[nextFreeUnAckIndex] += strlen(DAT);
			int n = sprintf(temp, "%d", seqNum);
			strncat(buffer, temp, n);
			strncat(buffer, " ", 1);
			packetSize[nextFreeUnAckIndex] += (n+1);
			memset(&temp, 0, sizeof temp);
			n = sprintf(temp, "%d", r);
			strncat(buffer, temp, n);
			strcat(buffer, " \n\n");
			packetSize[nextFreeUnAckIndex] += (n+3);
			char *b = buffer+packetSize[nextFreeUnAckIndex];
			memcpy(b, readBuffer, r);
			packetSize[nextFreeUnAckIndex] += r;
			memcpy(unAckData[nextFreeUnAckIndex], buffer, sizeof buffer);
			unAckSeqNum[nextFreeUnAckIndex] = seqNum;
			expectedAckNum[nextFreeUnAckIndex] = (seqNum + payload)%MAXSEQ;
			//send the DAT packet to receiver
			sendToReceiver(buffer, packetSize[nextFreeUnAckIndex]);
			senderLog(0, 0, seqNum, payload);
			//assign time stamp for the dat packet
			sentTime[nextFreeUnAckIndex].tv_sec = current.tv_sec;
			sentTime[nextFreeUnAckIndex].tv_usec = current.tv_usec;
			nextFreeUnAckIndex = (nextFreeUnAckIndex + 1)%MAXPACKET;
			seqNum = (seqNum + r) % MAXSEQ;
			totalPacket++;
			uniquePacket++;
			usedWindow++;
		}else{
			//sender buffer is full, need to wait for ack packet
			receiveFromReceiver();
		}
	}
	//wait for ack number for all the unacknowledged packets including the FIN packet
	while(expectedAckNum[unAckOldestIndex] != -1){	
		timer.tv_sec = 0;
		timer.tv_usec = DEFAULTTIMEOUT;
		if(isTimeout() == TRUE){
			receiveFromReceiver();
			while(resendAckNum != ackNum){
				receiveFromReceiver();				
			}
		}else{
			receiveFromReceiver();
		}
	}
	//finish sending data, send FIN packet
	sendFIN();
	fclose(f);
}

int main (int argc, char *argv[]){
	//check the number of arguments
	if(argc != 6){
		printf("Usage: \"./rdps <sender_ip> <sender_port> <receiver_ip>"); 
		printf(" <receiver_port> <sender_file_name>\"\n"); 
		return -1;
	}
	openSocket();
	reusePort();
	initSockaddr(argv[1], argv[2],argv[3], argv[4]);
	bindAddr();
	//set initial random sequence number randomly
	srand(time(NULL));
	seqNum = rand() % MAXSEQ;
	//initialize buffer
	memset(&unAckData, 0, sizeof unAckData);
	memset(&unAckSeqNum, -1, sizeof unAckSeqNum);
	memset(&expectedAckNum, -1, sizeof expectedAckNum);
	memset(&packetSize, 0, sizeof packetSize);
	//start sending packets
	sendSyn();	
	sendData(argv[5]);
	summary();
	close(sock);
	return 0;
}
