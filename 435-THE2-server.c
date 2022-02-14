#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>

#define MAXLINE 258
#define INVERT 0xFFFFFF
#define WINDOW_SIZE 4
#define TIME_OUT 100000
#define TEN_POWER_6 1000000
#define BYE_SIGNAL 1
#define CONT_SIGNAL 0

/* START OF SOCKET BINDING*/

// int Create_socket() initializes and returns a socket which is int.
int create_socket()
{

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	if(sockfd < 0)
		printf("A problem occured initializing socket.\n");

	return sockfd;
}

// void bind_socket() binds the given socket with the given addres and port
void bind_socket(int sockfd, struct sockaddr_in* addr)
{
	int flag = bind(sockfd, (const struct sockaddr *) addr, sizeof(*addr));

	if (flag < 0)
		printf("A problem occured at bind_socket.\n");

	return;
}

// int init_comm socket() takes the port number and an addres to 
// sockaddr_in variable then it initalizes addres with given portnumber.
int init_comm_socket(int port, struct sockaddr_in* addr)
{

	if(!addr)
	{
		printf("init_comm_socket: parameter addr is NULL.\n");
		return -1;
	}

	int sockfd = create_socket();

	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = INADDR_ANY;
	addr->sin_port = htons(port);

	bind_socket(sockfd, addr);

	return sockfd;
}
/* END OF SOCKET BINDING*/

/* START OF PACKET COMMUNICATION*/
int SEQ_NUM = 0; // This parameter is needed to keep track of seq_num 
				 // over the initalization of all packets.

// This is struct is used to send data between client and server.
struct udp_packet 
{
    int checksum; //int checksum is kept for noise check.
	
	short seq_num;  //seq_num is used to inform the receiver which sequence
				    //of data it is receiving.
					// seq_num == -1 means it's an ACK packet.
	
	char buff[8]; //char buff[8] stores 8 bytes of data which is going to be sent.
};

// struct packet_window is used to keep track 
// which sequences of data is sent or received.
struct packet_window
{
	// an udp_packet* array which stores the sequence of 
	// packets that are supposed to be sent.
	struct udp_packet* packet_arr[WINDOW_SIZE];
	
	short seq_head; // short seq_head is used at both receiving and sending
					// it keeps	track of which sequence of data 
					// we are supposed to receive and send.
					// for sender window it is between 0 - WINDOW_SIZE-1.
					// for receiver window it is between 0 - 2*WINDOWS_SIZE-1.
	
	char ack_arr[WINDOW_SIZE]; // used to keep track of which 
							   // sequences of data is ACKed.
							   // only used at send_window parameter.
							   // ack_arr[i] == -1 means waiting for ACK.
							   // ack_arr[i] == 0 means not sent.
							   // ack_arr[i] == 1 means received ACK.
							   // thinking now it would be a good idea
							   // to use #define for these values.

	short send_head; // keeps track up to which sequence of data
					 // has been sent.
	
	struct timeval time_sent[8]; // used to check if the packet had timed out.
};

// struct node and struct queue is used to implement a packet queue which
// stores packets of data that are waiting to be sent.
struct node
{
	struct udp_packet* packet;
	struct node* next;
	struct node* prev;
};

struct queue 
{
	struct node* head;
	int size;
};

// struct recv_buffer is used because I either was going use a global variable
// for current index or make it a struct it is only used for receiving you can
// check int extract_data() why I used this struct.
struct recv_buffer
{
	char buffer[MAXLINE]; // char buffer[MAXLINE] is a buffer which 
						  // stores the message until '\0' charachter is written.
	
	int curr_idx; // int curr_idx is used to keep track at which point
				  // int extract_data() is writing received packets.
};

// void insertqueue() is used to insert packets to the queue.
void insertqueue(struct queue* q, struct udp_packet* packet)
{
	struct node *new_node = malloc(sizeof(struct node));
	new_node->packet = packet;
		
	if(!q->head)
	{
		q->head = new_node;
		new_node->prev = new_node;
		new_node->next = new_node;
		q->size += 1;
		return;
	}

	new_node->prev = q->head;
	new_node->next = q->head->next;

	q->size += 1;
	q->head->next->prev = new_node;
	q->head->next = new_node;

	return;
}

// struct udp_packet* popqueue() is used to pop packets from the queue.
struct udp_packet* popqueue(struct queue* q)
{
	if(!(q->head))
		return NULL;

	struct udp_packet* packet = q->head->packet;
	struct node* prev_head = q->head;

	q->head->next->prev = q->head->prev;
	q->head->prev->next = q->head->next;

	if(q->size == 1)
		q->head = NULL;

	else
		q->head = q->head->prev;

	q->size -= 1;
	prev_head->next = NULL;
	prev_head->prev = NULL;

	free(prev_head);

	return packet;
}

// I guess it's pretty self explanatory.
int calculate_checksum(struct udp_packet* packet)
{
    int checksum = 0;

	checksum += packet->seq_num;

	for(int i = 0; i < 8; i++)
		checksum += packet->buff[i];

	checksum = checksum ^ INVERT;

	return checksum;

}

void load_checksum(struct udp_packet* packet)
{
	packet->checksum = calculate_checksum(packet);
	return;
}

// void make_packet() divides the input in the buffer to packets 
// and inserts them to the queue.
void make_packet(struct queue* packet_queue, char* buff, unsigned char buff_size)
{
	// calculating short packet_num this way helps the function to find if 
	// there are exxcesive charachters at the end of the message that
	// have size smaller than 8.
	short packet_num = (buff_size/8) + !!(buff_size % 8);
	
	struct udp_packet *packet;
	int i;

	for(i = 0; i < packet_num-1; i++)
	{
		packet = malloc(sizeof(struct udp_packet)); // allocate memory for packet
		memset(packet, 0, sizeof(struct udp_packet)); // and set it to 0.

		memcpy(packet->buff, buff + 8*i, 8); // copy 8 charachters from buffer
		packet->seq_num = SEQ_NUM++; // initialize it's seq_num.
		SEQ_NUM %= WINDOW_SIZE*2; // since seq_num is  mod 2*WINDOW_SIZE.
		packet->checksum = calculate_checksum(packet); // load packets checksum.

		insertqueue(packet_queue, packet); // insert packet to the queue.
	}

	// do the same thing one last time.
	packet = malloc(sizeof(struct udp_packet));
	memset(packet, 0, sizeof(struct udp_packet));

	memcpy(packet->buff, buff + 8*i, buff_size - 8*i);
	packet->seq_num = SEQ_NUM++; SEQ_NUM %= WINDOW_SIZE*2;
	packet->checksum = calculate_checksum(packet);

	insertqueue(packet_queue, packet);

	return;
}

// void recv_packet() helps me omit the same parameters that are used in recvfrom.
// when it's called it sends 1 packet.
void recv_packet(int sockfd, struct udp_packet* packet, struct sockaddr_in* addr, int* len)
{
	
	if(!addr)
	{
		printf("recv_packet: parameter addr is NULL.\n");
		return;
	}

	int n = 0;

	*len = sizeof(*addr);
	recvfrom(sockfd, packet, sizeof(struct udp_packet), MSG_WAITALL, 
			 (struct sockaddr*) addr, len);

	return;
}

// void send_packet() helps me omit the same parameters that are used in sendto.
// when it's called it sends 1 packet.
void send_packet(int sockfd, struct udp_packet* packet, struct sockaddr_in* addr, int* len)
{

	if(!addr)
	{
		printf("send_packet: parameter addr is NULL.\n");
		return;
	}

	*len = sizeof(*addr);
	sendto(sockfd, packet, sizeof(struct udp_packet), MSG_CONFIRM, 
		   (const struct sockaddr *) addr, *len);

	return;
}

// void redt_send() sends udp_packets with ack_arr[i] == 0
// and sets the time they are sent for checking timeout.
void rdt_send(int sockfd, struct packet_window* send_window, 
			  struct sockaddr_in* addr, int*len)
{
	struct udp_packet* packet = NULL;

	// check if send_head is already at the same point with seq_head
	// and if they are check if the data at seq_head is sent.
	// I check the second part because filling all the adress at packet
	// window at once bring seq_head to the same point it started
	// which also is pointed by send_head but these newly filled
	// points were never sent. 
	// We don't need to send packets after we reach seq_head since 
	// it points to the last packet that is going to be sent.
	while(send_window->send_head != send_window->seq_head ||
		  send_window->ack_arr[send_window->seq_head] == 0) 
	{
		int i = send_window->send_head;
		send_window->send_head = (send_window->send_head + 1) % WINDOW_SIZE;

		packet = send_window->packet_arr[i];

		if(send_window->ack_arr[i] || packet == NULL)
			continue;

		send_window->ack_arr[i] = -1; 

		// FOR DEBUGGING PURPOSES
		//printf("rdt_send with: seq_num = %d, checksum = %d, content: ", 
		//	packet->seq_num, packet->checksum);
		//	for(int i = 0; i < 8; i++)
		//		printf("%c",packet->buff[i]);
		//printf("\n");

		send_packet(sockfd, packet, addr, len);
		gettimeofday(&(send_window->time_sent[i]), NULL); // initalize time.

	}

}

// void send_ACK() sends the ACK of the received packet if it is not skeved.
void send_ACK(int sockfd, struct sockaddr_in* addr, int* len, short seq_num)
{
	struct udp_packet ACK_packet;

	memset(&ACK_packet, 0, sizeof(struct udp_packet));
	ACK_packet.seq_num = -1;
	sprintf(ACK_packet.buff,"%d", seq_num);
	load_checksum(&ACK_packet);

	send_packet(sockfd, &ACK_packet, addr, len);
}

// short rdt_receive() receives a packet and does neccesary things.
short rdt_receive(int sockfd, struct packet_window* recv_window,
				  struct sockaddr_in* addr, int* len)
{
	struct udp_packet *packet = malloc(sizeof(struct udp_packet));
	memset(packet, 0, sizeof(struct udp_packet));

	recv_packet(sockfd, packet, addr, len);

	if(packet->checksum != calculate_checksum(packet))
	{	
		free(packet);
		return -2; // skeved packet.
	}

	if(packet->seq_num == -1) // it is an ACK.
	{
		int curr_ack = atoi(packet->buff) % WINDOW_SIZE;
		free(packet);
		return curr_ack; // return which packet is ACKed.
	}

	send_ACK(sockfd, addr, len, packet->seq_num);

	// Calculate the boundary of the receive sequneces.
	short seq_head = recv_window->seq_head, 
	      seq_tail = (recv_window->seq_head + WINDOW_SIZE - 1) % (WINDOW_SIZE*2),
	      seq_num = packet->seq_num;

	// FOR DEBUGGING PURPOSES
	//printf("seq_head = %d, seq_tail = %d\n", seq_head, seq_tail);

	int flag = (seq_head < seq_tail && seq_head <= seq_num && seq_num <= seq_tail)
			|| (seq_tail < seq_head && ((seq_head <= seq_num && seq_num <= 16) 
									 || (0 <= seq_num && seq_num <= seq_tail)));
	
	if(!flag) //Check if packet is out of seq boundary.
	{
		// FOR DEBUGGING PURPOSES
		//printf("Rejected Packet with seq_num = %d, seq_head = %d, seq_tail = %d",
		//		packet->seq_num, seq_head, seq_tail);

		free(packet);
		return -2; // data belongs to the sequence that is going to be received.
	}

	if(recv_window->packet_arr[packet->seq_num % WINDOW_SIZE])
	{
		// FOR DEBUGGING PURPOSES
		//printf("PACKET RESENT.\n");
		free(packet);
		return -2; // packet already received.
	}

	// FOR DEBUGGING PURPOSES
	//printf("rdt_receive with: seq_num = %d, checksum= %d, content: ", 
	//	    packet->seq_num, packet->checksum);
	//for(int i = 0; i < 8; i++)
	//	printf("%c",packet->buff[i]);
	//printf("\n");

	recv_window->packet_arr[packet->seq_num % WINDOW_SIZE] = packet;
	
	return -1; // recieved some data.

}

// int extact_data() writes the data in the recv_window to a buffer before it
// is in a displayeble form and returns if the server/client should keep the
// communicatiom.
int extract_data(struct recv_buffer* recv_buff, struct packet_window* recv_window)
{
	struct udp_packet *curr_packet = NULL;

	// check if the packet that was supposed to be received first has arrived.
	while(recv_window->packet_arr[recv_window->seq_head % WINDOW_SIZE])
	{
		curr_packet = recv_window->packet_arr[recv_window->seq_head % WINDOW_SIZE];

		if(!curr_packet)
			printf("extract_data: curr_packet parameter is NULL.\n");

		// empty the receive window.
		recv_window->packet_arr[recv_window->seq_head % WINDOW_SIZE] = NULL;

		// write packet content to the buffer.
		for(int i = 0; i < 8; i++)
		{
			recv_buff->buffer[recv_buff->curr_idx] = curr_packet->buff[i];
			
			// check if the '\0' charachter is received
			// if it is received then this is the end of the message.
			if(curr_packet->buff[i] == '\0')
			{
				//print out the buffer.
				printf("%s", recv_buff->buffer);

				if(recv_buff->curr_idx == 4 && // check if BYE.
				   strcmp(recv_buff->buffer, "BYE\n\0") == 0)
				{
					free(curr_packet);
					return BYE_SIGNAL; // Tell server to stop.
				}

				//empty the buffer.
				recv_buff->curr_idx = 0;
				memset(recv_buff->buffer, 0, MAXLINE);

				break;
			}

			recv_buff->curr_idx += 1;
		}

		recv_window->seq_head = (recv_window->seq_head + 1) % (2*WINDOW_SIZE);
		free(curr_packet);
	}

	return CONT_SIGNAL; // Tell server to keep going.
}

// void update_send_window() updates the shifts the sender window depending
// on which packets are ACKed.
void update_send_window(struct queue* out_queue, struct packet_window* send_window)
{
	while(send_window->ack_arr[send_window->seq_head] == 1)
	{
		if(send_window->packet_arr[send_window->seq_head])
			free(send_window->packet_arr[send_window->seq_head]);
		
		send_window->packet_arr[send_window->seq_head] = popqueue(out_queue);

		if(send_window->packet_arr[send_window->seq_head])
			send_window->ack_arr[send_window->seq_head] = 0;

		else
		{
			send_window->ack_arr[send_window->seq_head] = 1;
			return;
		}

		send_window->seq_head = (send_window->seq_head + 1) % WINDOW_SIZE;
	}
	
	return;
}

// long get_time_dif() gets the time difference in microseconds.
long get_time_dif(struct timeval *packet_time, struct timeval *curr_time)
{
	return (curr_time->tv_sec * TEN_POWER_6 + packet_time->tv_usec) -
	       (packet_time->tv_sec * TEN_POWER_6 + packet_time->tv_usec);
}

// void send_time_outs() checks if packets that are waiting to be ACKed
// has timed out.
void send_time_outs(int sockfd, struct packet_window* send_window, 
			  		struct sockaddr_in* addr, int*len)
{
	struct timeval curr_time;
	struct udp_packet *curr_packet = NULL;
	gettimeofday(&curr_time, NULL);

	for(int i = 0; i < 8; i++)
	{

		if(get_time_dif( &(send_window->time_sent[i]), &curr_time) < TIME_OUT ||
		   send_window->ack_arr[i] != -1 || send_window->packet_arr[i] == NULL)
			continue;

		curr_packet = send_window->packet_arr[i];

		send_packet(sockfd, curr_packet, addr, len);
		gettimeofday(&(send_window->time_sent[i]), NULL);

	}
}

//void establish_comm() regulates the communication between client and server.
void establish_comm(int sockfd, struct sockaddr_in* addr, int* len)
{
	int num_events = -1;
	struct pollfd pfds[2];
	struct packet_window recv_window, send_window;
	struct queue out_queue;
	char send_buff[MAXLINE];
	struct recv_buffer recv_buff;

	memset(&recv_window, 0, sizeof(recv_window));
	memset(&send_window, 0, sizeof(send_window));
	memset(send_window.ack_arr, 1, 8);

	recv_buff.curr_idx = 0;

	out_queue.head = NULL;
	out_queue.size = 0;

	pfds[0].fd = sockfd;
	pfds[0].events = POLLIN;

	pfds[1].fd = STDIN_FILENO;
	pfds[1].events = POLLIN;

	while(1)
	{

		num_events = poll(pfds, 2, 100);

		int sock_flag = pfds[0].revents & POLLIN & num_events;
		int stdio_flag = pfds[1].revents & POLLIN & num_events;
		
		if(stdio_flag) // check if standart input buffer is not empty.
		{
			int is_bye = CONT_SIGNAL;

			fgets(send_buff, MAXLINE, stdin);

			if(strcmp(send_buff, "BYE\n") == 0)
				is_bye = BYE_SIGNAL; // if BYE sent terminate comms.

			make_packet(&out_queue, send_buff, strlen(send_buff) + 1);

			update_send_window(&out_queue, &send_window);

			rdt_send(sockfd, &send_window, addr, len);

			if(is_bye == BYE_SIGNAL)
				return; // if BYE sent terminate comms.			
		}

		if(sock_flag) // check if got data from socket.
		{

			int flag = 0;
			int is_bye;

			flag = rdt_receive(sockfd, &recv_window, addr, len);

			if(flag == -1) // Got an ACK.
			{
				is_bye = extract_data(&recv_buff, &recv_window);

				if(is_bye == BYE_SIGNAL)
					return; // if BYE received terminate comms.
			}

			else if(flag >= 0) // Got some some data.
			{
				send_window.ack_arr[flag] = 1;
				update_send_window(&out_queue, &send_window);
				rdt_send(sockfd, &send_window, addr, len);
			}
		}

		// check if some packets timed out.
		send_time_outs(sockfd, &send_window, addr, len);
	}

	return;
}
/* END OF PACKET COMMUNICATION*/

int main(int argc, char *argv[])
{
	int sockfd, port;
	struct sockaddr_in servaddr, cliaddr;
	char *port_str = argv[1];

	port = atoi(port_str);

	// FOR DEBUGGING PURPOSES
	//printf("bind_port: %d\n", port);
	
	memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

	sockfd = init_comm_socket(port, &servaddr);

	int len = 0;

	establish_comm(sockfd, &cliaddr, &len);

	close(sockfd);
	return 0;
}
