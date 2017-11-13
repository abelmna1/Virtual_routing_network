/*--------------------------------------------------------------------*/
/* Route utils */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <vector>

typedef struct {
	char source_node_id;
	char dest_node_id;
	char packet_id;
	char ttl;
} Data_Header;

typedef struct {
	char node_path[40];
} Data_Payload;

typedef struct {
	char source_node_id;
	char dest_node_id;
} Control_Header;

typedef struct{
	char reachable_nodes[40];
	char costs[40];
} Control_Payload;

// sets up router with UDP socket
int start_router(int control_port, char * name){
	int sd;
	struct sockaddr_in router_addr;
	ushort port = (ushort)control_port;

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sd == -1){
		perror("socket");
		exit(1);
	}

	struct hostent* host_entry;
	host_entry = gethostbyname(name);

	if(!host_entry){
		perror("gethostbyname");
		exit(1);
	}

	char ip[1024];
	struct in_addr ** addr_list;
	addr_list = (struct in_addr **) host_entry->h_addr_list;
	strcpy(ip, inet_ntoa(*addr_list[0]));

	memset(&router_addr, 0, sizeof(struct sockaddr_in));
	router_addr.sin_family = AF_INET;
	router_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(control_port == -2){
		router_addr.sin_port = htons(0);
	}	
	else router_addr.sin_port = htons(port);

	if( bind(sd, (struct sockaddr*) &router_addr, sizeof(router_addr)) == -1){
		perror("bind");
		exit(1);
	}
	return sd;
}
