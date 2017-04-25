# CSC361RDP
CSC361 Reliable Datagram Protocol

[Project Specification](p2.pdf)

V00796911

Design Questions
1) How do you design and implement your RDP header and header fields? Do you use any additional header fields? 
using char array with size 1024

SYN PACKET:
"CSC361SYN initSeqNum \n\n"

FIN PACKET:
"CSC361FIN seqNum \n\n"

ACK PACKET:
"CSC361ACK ackNum windowSize \n\n"
windowSize is initially set at 10240  

RST PACKET:
"CSC361RST \n\n"

DAT PACKET:
"CSC361DAT seqNum payloadLength \n\ndata"

Each packet has maximum of 1024 bytes 
	10 bytes for CSC361+type+space (eg. "CSC361ACK ")
	10 bytes for seqNum/ackNum+space (0 to 999999999)
	2 bytes for newline ("\n\n")
For data packet
	10 bytes for CSC361DAT+space
	10 bytes for seqNum+space
	4 bytes for payloadLength+space
	3 bytes for newline
	1 bytes for terminating null character
	So the maximum number of payloadLength is 996 for each data packet
	
No additional header field

2) How do you design and implement the connection management using SYN, FIN and RST packets? How to choose the initial sequence number?

sender send a SYN packet where initial sequence number is chosen randomly between 0 to 999999999 (up to 9 digits)
once receiver receive a SYN packet from sender it will store the initial sequence number and send an ACK packet back where ackNum = initSeqNum + 1
sender will send up to 5 SYN packets and wait for 2 seconds in between packets, if there is no response from the receiver it'll close the connection
receiver will wait for 20 seconds for SYN packet, if there's nothing then it'll close the connection	
	
when sender finish sending data and it has received the ack for the last packet, it'll send a FIN packet
when the receiver receive a FIN packet, it will send an ack number for the FIN packet if it had received all the data packets, wait for 2 seconds in case the sender did not get the ack number for FIN, then close the connection.
if the receiver received a FIN packet while it's still waiting for missing packet, it'll drop the FIN and send the cumulative ack back for the missing packet (next expected sequence number)
sender close a connection once it receive the last ack number for FIN
	
I'm using select() as a timer to detect if there is a DAT/ACK packet waiting for both the sender and the receiver.
For the sender side, the select() timer is set at 7000 microseconds. If the sender received nothing in 7000 microseconds (ie select timeout), it'll resend the oldest unacknowledge packet, this process will repeat 5 times if there's still no response from the receiver, it'll send a RST packet and close the connection.
For the receiver side, the select() timer is set a 1 second, if the receiver received nothing in 1 second, it'll resend the ack packet,  this process will repeat 5 times if there's still no response fromm the sender, the forth time it timeout, it'll send a RST packet and close the connection

3) How do you design and implement the flow control using window size? How to choose the initial window size and adjust the size? How to read and write the file and how to manage the buffer at the sender and receiver side, respectively?

receiver have a buffer with a window size of 10240, approximately 10 packets, when it received an in order packet, it'll read the header, store the data in the buffer and decrease the window size by the number of payload length, then it'll send an ACK packet back to the sender with the new window size

if the receiver received out of order packets, the packet will be drop, because of this, to keep the number of retransmit packets at a minimum, the sender is only able to send up to 5 packets before it needs to wait for an ack packet. so the sender will reader a chunk of file for one packet (max 996 bytes of data), append it to the header, this packet will then be store in a buffer of maximum of size 5 and the packet will be send to the receiver. if the sender receive an acknowledgement number for this packet, it'll be remove from the buffer and the buffer size will increase so it can send a another packet. 

the receiver will write to the file when the buffer has a window size of less than 1024 and window size go back to 10240, it'll write to file when window size is less than 1024 to avoid advertise small window size to the sender
	
4) How do you design and implement the error detection, notification and recovery? How to use timer? How many timers do you use? How to respond to the events at the sender and receiver side, respectively? How to ensure reliable data transfer?

There is a time stamp for every data packets that was sent out. The packet will be retransmitted if (current time - timestamp) > 6*round trip time where the round trip time measure from the first syn and ack. 6*round trip time is chosen because the network could become more congested compare to the connection establishment state and also since I'm allowing the sender to send 5 packets before it need an ack packet.

nothing happen if the sender receive duplicate ack, a packet will only be retransmit if it timeout on the sender side and a new timestamp is assigned to the packet
 
every data packets received, the receiver will send a cumulative ack back to the sender
 
if the receiver received an out of order packet, the packet will be drop and the receiver will send an ACK packet back where the acknowledgement number is for the missing packet

if the receive received a duplicate packet, the packet will also be drop and it'll send an ACK packet back where the cumulative acknowledgement number is for the next byte it's expecting

5) Any additional design and implementation considerations you want to get feedback from your lab instructor?

No


----------------------------------------------------------------

how to run:
./rdpr receiver_ip receiver_port receiver_file_name
./rdps sender_ip sender_port receiver_ip receiver_port sender_file_name

example of how to run:
"./rdpr 10.1.1.100 8080 receive.dat"
"./rdps 192.168.1.100 8080 10.10.1.100 8080 sent.dat"

receiver should be run before sender but it'll work if sender runs before receiver if you're fast enough to run receiver right after sender

the sender and receiver will terminate itself EVENTUALLY, whether by sending and receiving reset or if the file finish transferring
the process of the transferring file could take up to minutes