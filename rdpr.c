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
#define MAXWIN 10240
#define MAXSEQ 1000000000
#define PARAM 10
#define TRUE 1
#define FALSE 0

const char *eventType[4] = {"s", "S", "r", "R"};
const char *packetType[5] = {"DAT", "ACK", "SYN", "FIN", "RST"};
const char RSTpacket[] = "CSC361RST \n\n";
int ackNum = -1;
int seqNum = -1;
int actualWin = MAXWIN;
int payload = 0;
int dataInBuff = 0;
char data[MAXWIN];
int selectTimeout = 0;
int synComplete = FALSE;

int finRecved = FALSE;
int inOrderPack = 0;
int totalData = 0;
int uniqueData = 0;
int totalPacket = 0;
int uniquePacket = 0;
int numSynRecv = 0;
int numFinRecv = 0;
int numRstRecv = 0;
int numAckSend = 0;
int numRstSend = 0;

int sock;
struct sockaddr_in rcv;
struct sockaddr_in snd;
struct tm *getTime;
struct timeval currentTime;
struct timeval startTime;
FILE *f;

void openSocket (){
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

void initSockaddr(char* rIP, char* rPort){
	memset(&rcv, 0, sizeof(rcv));
	rcv.sin_family = AF_INET;
	rcv.sin_addr.s_addr = inet_addr(rIP);
	rcv.sin_port = htons(atoi(rPort));
	memset(&snd, 0, sizeof(snd));
}

void bindAddr(){
	if(bind(sock, (struct sockaddr *) &rcv, sizeof(rcv)) == -1){
		perror("Failed to bind");
		close(sock);
		exit(EXIT_FAILURE);
	}
}

void receiverLog(int event, int packet){
	char buffer[10];
	memset(&buffer, 0, sizeof buffer);
	gettimeofday(&currentTime, NULL);
	getTime = localtime(&currentTime.tv_sec);
	strftime(buffer, 10, "%H:%M:%S", getTime);
	printf("%s.%d %s ", buffer, (int)currentTime.tv_usec, eventType[event]);
	printf("%s:%d ", inet_ntoa(rcv.sin_addr), ntohs(rcv.sin_port));
	printf("%s:%d ", inet_ntoa(snd.sin_addr), ntohs(snd.sin_port));
	printf("%s ", packetType[packet]);
	if(packet == 1){
		printf("%d ", ackNum);
		printf("%d\n", actualWin);
	}else if(packet == 4){
		printf("\n");
	}else{
		printf("%d ", seqNum);
		printf("%d\n", payload);
	}		
}

//get the time different in seconds
double timeDiff(struct timeval start, struct timeval end){
	double s, e, d;
	s = (double)start.tv_sec + (double)start.tv_usec/1000000.0;
	e = (double)end.tv_sec + (double)end.tv_usec/1000000.0;
	d = (double)e - (double)s;
	return d;
}

void summary(){
	printf("total data bytes received: %d\n", totalData);
	printf("unique data bytes received: %d\n", uniqueData);
	printf("total data packets received: %d\n", totalPacket);
	printf("unique data packets received: %d\n", uniquePacket);
	printf("SYN packets received: %d\n", numSynRecv);
	printf("FIN packets received: %d\n", numFinRecv);
	printf("RST packets received: %d\n", numRstRecv);
	printf("ACK packets sent: %d\n", numAckSend);
	printf("RST packet sent: %d\n", numRstSend);
	printf("total time duration (second): %f\n", timeDiff(startTime, currentTime));
}

void sendToSender(const char *buffer){
	if(sendto(sock, buffer, strlen(buffer), 0, (struct sockaddr*)&snd, sizeof snd) == -1){
		perror("sendto failed");
		close(sock);
		exit(EXIT_FAILURE);
	}
}

//send ACK packet: "CSC361ACK ackNum windowSize"
void sendACK(int event){
	char buffer[MAX];
	char b[10];
	memset(&buffer, 0, sizeof buffer);
	memset(&b, 0, sizeof b);
	strcat(buffer, "CSC361ACK ");
	int n = sprintf(b, "%d", ackNum);
	strncat(buffer, b, n);
	strcat(buffer, " ");
	memset(&b, 0, sizeof b);
	n = sprintf(b, "%d", actualWin);
	strncat(buffer, b, n);
	strcat(buffer, "\n\n");
	sendToSender(buffer);
	receiverLog(event, 1);
	numAckSend++;
}

//received syn packet
void isSYN(const char *buffer){
	char temp[MAX];
	memset(&temp, 0, sizeof temp);
	strncpy(temp, buffer, strlen(buffer));
	strtok(temp, " ");
	seqNum = atoi(strtok(NULL, " "));
	//send ack packet
	if(ackNum == -1){
		//receive syn packet for the first time
		receiverLog(2, 2);
		startTime.tv_sec = currentTime.tv_sec;
		startTime.tv_usec = currentTime.tv_usec;
		ackNum = (seqNum + 1) % MAXSEQ;
		sendACK(0);
	}else{
		//receive duplicate syn packet
		receiverLog(3, 2);
		sendACK(1);
	}
}

//received dat packet
void isDAT(const char *buffer){
	//read header, get seqNum and payload length
	char temp[MAX];
	char *d;
	memset(&temp, 0, sizeof temp);
	strncpy(temp, buffer, strlen(buffer));
	char *token = strtok(temp, " ");
	seqNum = atoi(strtok(NULL, " "));
	token = strtok(NULL, " ");
	payload = atoi(token);
	totalData += payload;
	if(seqNum < ackNum){
		//duplicate dat packet
		receiverLog(3, 0);
		sendACK(1);
		return;
	}else if(seqNum > ackNum){
		//out of order dat packet
		receiverLog(2, 0);
		sendACK(1);
		return;
	}
	//expected dat packet, assign ack number
	uniqueData += payload;
	uniquePacket++;
	receiverLog(2, 0);
	ackNum = (seqNum + payload) % MAXSEQ;
	token = strtok(NULL, " ");
	//store data in buffer, decrease window size
	d = strchr(buffer, '\n');
	d = d+2;
	char *b = data + dataInBuff;
	memcpy(b, d, payload);
	dataInBuff += payload;
	actualWin = MAXWIN - dataInBuff;
	//write to file if window size is less than 1024 bytes
	if(actualWin < 1024){
		fwrite(data, sizeof(char), dataInBuff, f);
		memset(&data, 0, sizeof data);
		actualWin = MAXWIN;
		dataInBuff = 0;
	}
	sendACK(0);
}

//received FIN packet
void isFIN(const char *buffer){
	char temp[MAX];
	memset(&temp, 0, sizeof temp);
	strncpy(temp, buffer, strlen(buffer));
	strtok(temp, " ");
	seqNum = atoi(strtok(NULL, " "));
	finRecved = TRUE;
	if(seqNum < ackNum){
		//duplicate fin
		receiverLog(3, 3);
		sendACK(1);
		return;
	}else if(seqNum > ackNum){
		//out of order fin (received fin but haven't receive all the data packets)
		receiverLog(2, 3);
		sendACK(1);
		return;
	}
	//expected fin, send ack
	payload = 0;
	receiverLog(2, 3);
	ackNum = (seqNum + 1) % MAXSEQ;
	sendACK(0);
}

//receive rst packet
void isRST(const char *buffer){
	receiverLog(2, 4);
	numRstRecv++;
	summary();
	close(sock);
	exit(EXIT_FAILURE);
}

//receive packet from sender
void receiveFromSender(){
	char recvBuffer[MAX];
	char buffer[MAX];
	socklen_t fromlen = sizeof(snd);
	struct timeval timer;
	fd_set readfds;
	//initial timer = 20 seconds
	timer.tv_sec = 20;
	timer.tv_usec = 0;
	while(TRUE){
		FD_ZERO(&readfds);
		FD_SET(sock, &readfds);
		memset(&recvBuffer, 0, sizeof recvBuffer);
		memset(&buffer, 0, sizeof buffer);
		int selectReturn = select(sock+1, &readfds, NULL, NULL, &timer);
		if(selectReturn == -1){
			perror("select() failed");
			close(sock);
			exit(EXIT_FAILURE);
		}
		if(selectReturn == 0){
			//no response from the sender
			if(finRecved == TRUE){
				//received nothing after send last ack for fin
				break;
			}else if(ackNum == -1){
				//no syn packet received
				printf("NO RESPONSE\n");
				close(sock);
				exit(EXIT_FAILURE);
			}else if(selectTimeout < 5){
				//resend cumulative ack
				sendACK(1);
			}else{
				//select timeout 5 times after resend cumulative ack, send rst
				sendToSender(RSTpacket);
				receiverLog(0, 4);
				numRstSend++;
				summary();
				close(sock);
				exit(EXIT_FAILURE);
			}
			selectTimeout++;
			timer.tv_sec = 1;
			timer.tv_usec = 0;
		}else if(FD_ISSET(sock, &readfds)){
			timer.tv_sec = 1;
			timer.tv_usec = 0;
			//received something from sender
			if(recvfrom(sock, recvBuffer, sizeof recvBuffer, 0, (struct sockaddr*)&snd, &fromlen) == -1){
				perror("recvfrom failed");
				close(sock);
				exit(EXIT_FAILURE);
			}
			selectTimeout = 0;
			//read header, check what type of packet received
			memcpy(buffer, recvBuffer, MAX);
			char *token = strtok(recvBuffer, " ");
			if(strcmp(token, "CSC361SYN") == 0){
				//received syn packet
				isSYN(buffer);
				numSynRecv++;
				synComplete = TRUE;
			}else if(strcmp(token, "CSC361DAT") == 0){
				//received dat packet
				if(synComplete == FALSE){
					//received dat but did not receive syn, wait for syn
					continue;
				}
				isDAT(buffer);
				totalPacket++;
			}else if(strcmp(token, "CSC361FIN") == 0){
				//received fin packet
				if(synComplete == FALSE){
					//received fin but did not receive syn, wait for syn
					continue;
				}
				isFIN(buffer);
				numFinRecv++;
				timer.tv_sec = 2;
				timer.tv_usec = 0;
			}else if(strcmp(token, "CSC361RST") == 0){
				//received rst packet
				isRST(buffer);
			}else{
				//received unknown packet
				printf("Unknown packet received, not part of this connection\n");
			}
		}
	}
	//if there's still data in the buffer, write to file
	if(dataInBuff > 0){
		fwrite(data, sizeof(char), dataInBuff, f);
		memset(&data, 0, sizeof data);
		actualWin = MAXWIN;
		dataInBuff = 0;
	}
}

int main (int argc, char *argv[]){
	if(argc != 4){
		printf("Usage: \"./rdpr <receiver_ip> <receiver_port> <receiver_file_name>\"\n");
		return -1; 
	}
	openSocket();
	reusePort();
	initSockaddr(argv[1], argv[2]);
	bindAddr();
	memset(&data, 0, sizeof data);
	f = fopen(argv[3], "wb");
	if(f == NULL){
		printf("Cannot open file: %s\n", argv[3]);
		exit(EXIT_FAILURE);
	}
	receiveFromSender();
	summary();
	fclose(f);
	close(sock);  
	return 0;
}
