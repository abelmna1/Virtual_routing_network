/*--------------------------------------------------------------------*/
/* Distance vector routing & Data packet forwarding */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "route_utils.hpp"

#define MAXSIZE 1024

using namespace std;

int PACKET;
char MESSAGE_RECEIVED;
int CLIENT_ARRAY[2];
char * INPUT_FILENAME;
pthread_mutex_t mutex_lock;

typedef struct {
	char destination;	//node id
	char next_hop;		// port #
	char distance;		// cost
} Route; // each entry in a node's routing table (AKA a distance vector)

typedef struct{
	char node_id;
	char * host_name;
	int data_port;
	int control_port;
} Neighbor;

typedef struct {
	char id;
	int control_port;
  	int data_port;
  	char * hostname;
  	vector<Route> routing_table;
	vector<Neighbor> neighbors;
} Node;

/*-------------------------- CONTROL THREAD --------------------------*/

void delete_route(int destination_node, Node * node) {
	for(int i = 0; i < node->routing_table.size(); i++){
		if(node->routing_table[i].destination == destination_node){
			(node->routing_table).erase(node->routing_table.begin()+i);
			break;
		}
	}
}

char * create_delete_buffer(int destination_node, Node * node, Neighbor n) {
	int i = 0;
	char * c =  (char*) malloc(sizeof(Control_Header) + sizeof(Control_Payload));

	Control_Header header;
	Control_Payload payload;
	memset(payload.reachable_nodes, -5, sizeof(payload.reachable_nodes));
	memset(payload.costs, -5, sizeof(payload.costs));

	header.source_node_id = destination_node;
	header.dest_node_id = n.node_id;

	memcpy(c, &header, sizeof(Control_Header));
	memcpy(c+(sizeof(Control_Header)), &payload, sizeof(Control_Payload));

	return c;
}


void notify_to_delete(int destination_node, Node * node, int node_sock) { // deleting from neighbors
	struct hostent * host_entry;
	struct sockaddr_in router_addr;	
	socklen_t router_size = sizeof(router_addr);

	for(Neighbor n : node->neighbors){
		char * routing_buffer = create_delete_buffer(destination_node, node, n);
		memset((char*) &router_addr, 0, router_size);
		router_addr.sin_family = AF_INET;
		router_addr.sin_port = htons(n.control_port);
		host_entry = gethostbyname(n.host_name);
		if(!host_entry){
			perror("gethostbyname: send");
			exit(1);
		}
		memcpy(&router_addr.sin_addr, host_entry->h_addr, host_entry->h_length);

		if(sendto(node_sock, routing_buffer, (sizeof(Control_Header)+sizeof(Control_Payload)), 0, (struct sockaddr *) &router_addr, router_size) == -1){
			perror("sendto");
			exit(1);
		}
	}
	printf("\n***** UPDATED ROUTING TABLE AFTER DELETION ****\n");
	printf("(DESTINATION, NEXT HOP, DISTANCE)\n");
	for(int j=0; j < node->routing_table.size(); j++){
		printf("(%d,%d,%d) ", node->routing_table[j].destination, node->routing_table[j].next_hop, node->routing_table[j].distance);
	}
	printf("\n");
}

void delete_link(int source_node, int destination_node, Node * node, int node_sock) { // deleting from source
	if(node->id != source_node && node->id != destination_node){  //node not affected
		int destination_index = -1;
		int source_index = -1;

		for(int i = 0; i < node->neighbors.size(); i++){
			if(node->neighbors[i].node_id == source_node){
				source_index = i;
			}
			if(node->neighbors[i].node_id == destination_node){
				destination_index = i;
			}
		}
		if(source_index == -1){
			for (int j=0; j < node->routing_table.size(); j++) { // remove from routing table
				if (node->routing_table[j].destination == source_node) {
					(node->routing_table).erase(node->routing_table.begin()+j);
					j=0;
				}
				if (node->routing_table[j].next_hop == source_node || node->routing_table[j].next_hop == destination_node) {   //MIGHT NOT BE NECESSARY
					(node->routing_table).erase(node->routing_table.begin()+j);
					//notify_to_delete(node->routing_table[j].destination, node, node_sock);
					j=0;
				}
			}				
		}
		if(destination_index == -1){
			for (int j=0; j < node->routing_table.size(); j++) { // remove from routing table
				if (node->routing_table[j].destination == destination_node) {
					(node->routing_table).erase(node->routing_table.begin()+j);
					j=0;
				}
				if (node->routing_table[j].next_hop == destination_node || node->routing_table[j].next_hop == source_node) {   //MIGHT NOT BE NECESSARY
					(node->routing_table).erase(node->routing_table.begin()+j);
					//notify_to_delete(node->routing_table[j].destination, node, node_sock);
					j=0;
				}
			}
		}


	}
	
	else if (node->id == source_node) {
		for (int i=0; i < node->neighbors.size(); i++) { // remove from neighbors
			if (node->neighbors[i].node_id == destination_node) {
				(node->neighbors).erase(node->neighbors.begin()+i);
				i=0;
			}
		}

		for (int j=0; j < node->routing_table.size(); j++) { // remove from routing table
			if (node->routing_table[j].destination == destination_node) {
				(node->routing_table).erase(node->routing_table.begin()+j);
				j=0;
			}
			if (node->routing_table[j].next_hop == destination_node) {
				(node->routing_table).erase(node->routing_table.begin()+j);
				//notify_to_delete(node->routing_table[j].destination, node, node_sock);
				j=0;
			}
		}
		//notify_to_delete(destination_node, node, node_sock); // update neighbors
	}
	printf("\n***** UPDATED ROUTING TABLE AFTER DELETION ****\n");
	//printf("SOURCE NODE: %d\n", source_node); 
	printf("(DESTINATION, NEXT HOP, DISTANCE)\n");
	for(int j=0; j < node->routing_table.size(); j++){
		printf("(%d,%d,%d) ", node->routing_table[j].destination, node->routing_table[j].next_hop, node->routing_table[j].distance);
	}
	printf("\n");
	
}

// updates the local node's routing table based on a new route (p. 249 PBook)
void merge_route(vector<Route> &routing_table, Route new_route, char source_id) {
	
	int i;

	for (i=0; i<routing_table.size(); i++) {
		if(routing_table[i].destination == new_route.destination){
			if(new_route.distance + 1 < routing_table.at(i).distance){
				break; // found a better route
			}
			else if(source_id == routing_table.at(i).next_hop) {
				break; // metric for current next_hop may have changed
    		}
    			else {
      				return; // route is uninteresting, just ignore it
    			}
		}
	}
	if (i == routing_table.size()){  //route not in routing table
		Route r;
		routing_table.push_back(r);
	}

	routing_table.at(i) = new_route;
	routing_table.at(i).next_hop = source_id;
	++routing_table.at(i).distance;

}

char * create_control_buffer(Node * node, Neighbor n) {
	int i = 0;
	char * c =  (char*) malloc(sizeof(Control_Header) + sizeof(Control_Payload));

	Control_Header header;
	Control_Payload payload;
	memset(payload.reachable_nodes, -1, sizeof(payload.reachable_nodes));
	memset(payload.costs, -1, sizeof(payload.costs));

	header.source_node_id = node->id;
	header.dest_node_id = n.node_id;

	for (; i < node->routing_table.size(); i++) {
		payload.reachable_nodes[i] = node->routing_table[i].destination;
		payload.costs[i] = node->routing_table[i].distance;
	}

	memcpy(c, &header, sizeof(Control_Header));
	memcpy(c+(sizeof(Control_Header)), &payload, sizeof(Control_Payload));

	return c;
}

void update_neighbors(Node * node, int node_sock){
	struct sockaddr_in router_addr;
	struct hostent * host_entry;
	socklen_t router_size = sizeof(router_addr);

	for(Neighbor n : node->neighbors){
		char * routing_buffer = create_control_buffer(node, n);
		memset((char*) &router_addr, 0, router_size);
		router_addr.sin_family = AF_INET;
		router_addr.sin_port = htons(n.control_port);
		host_entry = gethostbyname(n.host_name);
		if(!host_entry){
			perror("gethostbyname: send");
			exit(1);
		}
		memcpy(&router_addr.sin_addr, host_entry->h_addr, host_entry->h_length);

		if(sendto(node_sock, routing_buffer, (sizeof(Control_Header)+sizeof(Control_Payload)), 0, (struct sockaddr *) &router_addr, router_size) == -1){
			perror("sendto");
			exit(1);
		}
	}
}

void create_link(int source_node, int destination_node, Node * node, int node_sock) {
	//printf("node->id: %d, source_node: %d, destination_node: %d\n", node->id, source_node, destination_node);

	if (source_node != destination_node){ //&& node->id == source_node) {
		
		/*for (Neighbor n : node->neighbors) {
			printf("INSIDE!!!!!!!!!!!\n");
			if (destination_node == n.node_id) return; // if neighbors, do nothing
		}*/

		// else, add route and parse file to get info to add neighbor
		Route r;
		r.destination = destination_node;
		r.next_hop = destination_node;
		r.distance = 1;
		
		printf("\n******* PROPOSED ROUTE FROM CREATE-LINK *******\n");
		printf("(DESTINATION, NEXT HOP, DISTANCE)\n");
		printf("(%d,%d,%d)\n", r.destination, r.next_hop, r.distance);
		merge_route(node->routing_table, r, destination_node);

		ifstream input_file(INPUT_FILENAME);
		if (!input_file.good()) {
			perror("Error opening file");
			exit(1);
		}

		Neighbor neighbor;
		int control_port, data_port, node_ID;
		string hostname, line;

		while(getline(input_file, line)) {
			istringstream ss(line);
			ss >> node_ID;
			if (node_ID == destination_node) { // add new neighbor
				ss >> hostname >> control_port >> data_port;
				neighbor.node_id = node_ID;
				char * temp_char = new char[hostname.length()+1];
				strcpy(temp_char, hostname.c_str());
				neighbor.host_name = temp_char;
				neighbor.control_port = control_port;
				neighbor.data_port = data_port;
				node->neighbors.push_back(neighbor);
				update_neighbors(node, node_sock);
				break;
			}
		}
		input_file.close();

	}
}

void init_distance_vector(Node * node){
	Route r;
	r.destination = node->id;
	r.next_hop = -1;
	r.distance = 0;
	node->routing_table.push_back(r);
	for(Neighbor n : node->neighbors){
		r.destination = n.node_id;
		r.next_hop = n.node_id;
		r.distance = 1;
		node->routing_table.push_back(r);
	}
}

void parse_control_buffer(char * routing_buffer, Node* node) {
	int next_hop, control_port;

	Control_Header header;
	Control_Payload payload;

	memcpy(&header, routing_buffer, sizeof(Control_Header));
	memcpy(&payload, (routing_buffer+sizeof(Control_Header)), sizeof(Control_Payload));

	memset(CLIENT_ARRAY, -1, sizeof(CLIENT_ARRAY));
	CLIENT_ARRAY[0] = header.source_node_id;
	CLIENT_ARRAY[1] = header.dest_node_id;
	
	char comparison = payload.reachable_nodes[0];	
	for (int k=0; k<sizeof(payload.reachable_nodes); k++) {
		if (payload.reachable_nodes[k] != comparison) {
			comparison = 0;
			break;
		}
	}

	if (comparison == -2) { // generate packet
		MESSAGE_RECEIVED = 2;
	}
	else if (comparison == -3) { // new link
		MESSAGE_RECEIVED = 3;
	}
	else if (comparison == -4) { // delete link
		MESSAGE_RECEIVED = 4;
	}
	else if (comparison == -5) { // delete link
		MESSAGE_RECEIVED = 5;
	}
	else { // distance vector routing
		Route r;
		r.next_hop = -2;
		MESSAGE_RECEIVED = 0;

		for (int i=0; i<(sizeof(payload.reachable_nodes)); i++) {
			if (payload.reachable_nodes[i] != -1) {
				r.destination = payload.reachable_nodes[i];
				r.distance = payload.costs[i];
				pthread_mutex_lock(&mutex_lock);
				merge_route(node->routing_table, r, header.source_node_id);
				pthread_mutex_unlock(&mutex_lock);
			}
			else {
				printf("\n************ UPDATED ROUTING TABLE ************\n");
				printf("(DESTINATION, NEXT HOP, DISTANCE)\n");
				for(int j=0; j < node->routing_table.size(); j++){
					printf("(%d,%d,%d) ", node->routing_table[j].destination, node->routing_table[j].next_hop, node->routing_table[j].distance);
				}
				printf("\n");
				break;
			}
		}
	}
}

static void* control(void * n) {
	Node * node = (Node *) n;

	fd_set rfds;
	FD_ZERO(&rfds);
	int node_sock = start_router(node->control_port, node->hostname);
	int livesdmax = node_sock+1;
	FD_SET(node_sock, &rfds);

	init_distance_vector(node);
	struct sockaddr_in router_addr;
	socklen_t router_size = sizeof(router_addr);
	struct timeval tv;
	time_t start, end;
	tv.tv_sec = 0;
	tv.tv_usec = 10000;

	char message[MAXSIZE];

	start = time(0);

	while(1) {
		memset((char*) &router_addr, -1, router_size);
		memset(message, -1, MAXSIZE);

		if(select(livesdmax, &rfds, NULL, NULL, &tv) == -1){
			perror("select");
			exit(1);
		}
		if(FD_ISSET(node_sock, &rfds)){
			if(recvfrom(node_sock, message, MAXSIZE, 0, (struct sockaddr *) &router_addr, &router_size) == -1){
				perror("recvfrom");
				exit(1);
			}
			parse_control_buffer(message, node);
		}
		end = time(0);
		if(difftime(end, start)*1000 > 5){
			start = end;
			update_neighbors(node, node_sock);
		}

		if (MESSAGE_RECEIVED == 3) { // CREATE LINK
			pthread_mutex_lock(&mutex_lock);
			MESSAGE_RECEIVED = 0;
			create_link(CLIENT_ARRAY[0], CLIENT_ARRAY[1], node, node_sock);
			memset(CLIENT_ARRAY, -1, sizeof(CLIENT_ARRAY));
			pthread_mutex_unlock(&mutex_lock);
		}
		else if (MESSAGE_RECEIVED == 4) { // DELETE LINK
			pthread_mutex_lock(&mutex_lock);
			MESSAGE_RECEIVED = 0;
			delete_link(CLIENT_ARRAY[0], CLIENT_ARRAY[1], node, node_sock);
			memset(CLIENT_ARRAY, -1, sizeof(CLIENT_ARRAY));
			pthread_mutex_unlock(&mutex_lock);
		}
		else if (MESSAGE_RECEIVED == 5) { // DELETE ROUTE
			MESSAGE_RECEIVED = 0;
			delete_route(CLIENT_ARRAY[0], node);
			memset(CLIENT_ARRAY, -1, sizeof(CLIENT_ARRAY));
		}

		FD_ZERO(&rfds);
		FD_SET(node_sock, &rfds);
	}
}

/*---------------------------- DATA THREAD ---------------------------*/

void forward_data_packet(char * data_packet, Node * node, int node_sock) {
	int j, k, found = 0;	
	struct sockaddr_in router_addr;
	struct hostent* host_entry;
	memset((char*) &router_addr, 0, sizeof(router_addr));
	
	Data_Header header;
	Data_Payload payload;
	memcpy(&header, data_packet, sizeof(Data_Header));
	memcpy(&payload, (data_packet+sizeof(Data_Header)), sizeof(Data_Payload));

	for(j = 0; j < node->neighbors.size(); j++){
		if(node->neighbors[j].node_id == header.dest_node_id){
			found = 1;
			break;
		}
	}
	
	if (found == 1) { // IF DIRECT NEIGHBOR, SEND PACKET
		router_addr.sin_family = AF_INET;
		router_addr.sin_port = htons(node->neighbors[j].data_port);
		host_entry = gethostbyname(node->neighbors[j].host_name);
		if(!host_entry){
			perror("gethostbyname: send");
			exit(1);
		}
		memcpy(&router_addr.sin_addr, host_entry->h_addr, host_entry->h_length);
		printf("*************** FORWARDED PACKET **************\n");
		printf("HEADER: [source_node_id: %d, dest_node_id: %d, packet_id: %d, ttl: %d]\n", header.source_node_id, header.dest_node_id, header.packet_id, header.ttl);
		if(sendto(node_sock, data_packet, (sizeof(Data_Header)+sizeof(Data_Payload)), 0, (struct sockaddr *) &router_addr, sizeof(router_addr)) == -1){
			perror("sendto");
			exit(1);
		}
	}
	else { // LOOKING FOR NEXT HOP TO GET TO DESTINATION NODE
		int next_hop_found = 0;
		for (int i = 0; i < node->routing_table.size(); i++) {
			if (header.dest_node_id == node->routing_table[i].destination) {
				for (k = 0; k < node->neighbors.size(); k++) {
					if (node->neighbors[k].node_id == node->routing_table[i].next_hop) {
						next_hop_found = 1;
						break;
					}
				}
				if (next_hop_found == 1) break;		
			}
		}
		if (next_hop_found == 1) {
			router_addr.sin_family = AF_INET;
			router_addr.sin_port = htons(node->neighbors[k].data_port);
			host_entry = gethostbyname(node->neighbors[k].host_name);
			if(!host_entry){
				perror("gethostbyname: send");
				exit(1);
			}
			memcpy(&router_addr.sin_addr, host_entry->h_addr, host_entry->h_length);
			printf("*************** FORWARDED PACKET **************\n");
			printf("HEADER: [source_node_id: %d, dest_node_id: %d, packet_id: %d, ttl: %d]\n", header.source_node_id, header.dest_node_id, header.packet_id, header.ttl);
			if(sendto(node_sock, data_packet, (sizeof(Data_Header)+sizeof(Data_Payload)), 0, (struct sockaddr *) &router_addr, sizeof(router_addr)) == -1){
				perror("sendto");
				exit(1);
			}
		}
	}
}

void generate_data_packet(int source_node, int destination_node, Node * node, int node_sock) {

	Data_Header header;
	Data_Payload payload;

	char * data_packet =  (char*) malloc(sizeof(Data_Header) + sizeof(Data_Payload));
	header.source_node_id = source_node;
	header.dest_node_id = destination_node;
	
	if (PACKET < 255) {
		header.packet_id = ++PACKET;
	}
	else {
		PACKET = 0;
		header.packet_id = PACKET;
	}
	
	header.ttl = 15;
	
	memset(payload.node_path, -1, sizeof(payload.node_path));
	payload.node_path[0] = source_node;

	memcpy(data_packet, &header, sizeof(Data_Header));
	memcpy(data_packet+(sizeof(Data_Header)), &payload, sizeof(Data_Payload));

	printf("*************** GENERATED PACKET **************\n");
	printf("HEADER: [source_node_id: %d, dest_node_id: %d, packet_id: %d, ttl: %d]\n", header.source_node_id, header.dest_node_id, header.packet_id, header.ttl);

	forward_data_packet(data_packet, node, node_sock);
}

void parse_data_buffer(char * data_packet, Node* node, int node_sock) { // when you receive

	int next_hop, control_port;
	int appended = 0;
	Data_Header header;
	Data_Payload payload;

	memcpy(&header, data_packet, sizeof(Data_Header));
	memcpy(&payload, (data_packet+sizeof(Data_Header)), sizeof(Data_Payload));

	if (header.ttl > 0) {
		printf("*************** RECEIVED PACKET ***************\n");
		printf("HEADER: [source_node_id: %d, dest_node_id: %d, packet_id: %d, ttl: %d]\n", header.source_node_id, header.dest_node_id, header.packet_id, header.ttl);
		if (header.dest_node_id == node->id) {
			printf("NODE PATH:");
			for (int i=0; i<(sizeof(payload.node_path)); i++) {
				if (payload.node_path[i] != -1) {
					printf(" %d", payload.node_path[i]);
				}
			}
			printf("\n");
		}

		for (int i=0; i<(sizeof(payload.node_path)); i++) {
			if (payload.node_path[i] == -1) {
				payload.node_path[i] = node->id;
				header.ttl = header.ttl - 1;
				appended = 1;
				break;
			}
		}
		if (appended == 1){  //successfully appended to node path
			if (header.dest_node_id == node->id) {		
				return;
			}
			else {
				char * updated_packet = (char*) malloc(sizeof(Data_Header) + sizeof(Data_Payload));
				memcpy(updated_packet, &header, sizeof(Data_Header));
				memcpy(updated_packet+(sizeof(Data_Header)), &payload, sizeof(Data_Payload));
			
				pthread_mutex_lock(&mutex_lock);
				forward_data_packet(updated_packet, node, node_sock);
				pthread_mutex_unlock(&mutex_lock);
			}
		}
	}
	else {
		printf("*************** DROPPED PACKET ****************\n");
		printf("HEADER: [source_node_id: %d, dest_node_id: %d, packet_id: %d, ttl: %d]\n", header.source_node_id, header.dest_node_id, header.packet_id, header.ttl);
		return;
	}
}

static void* data(void * n) {
	Node * node = (Node *) n;

	fd_set rfds;
	FD_ZERO(&rfds);
	int node_sock = start_router(node->data_port, node->hostname);
	int livesdmax = node_sock+1;
	FD_SET(node_sock, &rfds);

	struct sockaddr_in router_addr;
	socklen_t router_size = sizeof(router_addr);
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 10000;

	char message[MAXSIZE];

	while(1) {
		
		memset((char*) &router_addr, -1, router_size);
		memset(message, -1, MAXSIZE);

		if(select(livesdmax, &rfds, NULL, NULL, &tv) == -1){
			perror("select");
			exit(1);
		}
		if(FD_ISSET(node_sock, &rfds)){
			if(recvfrom(node_sock, message, MAXSIZE, 0, (struct sockaddr *) &router_addr, &router_size) == -1){
				perror("recvfrom");
				exit(1);
			}
			parse_data_buffer(message, node, node_sock);
		}
		
		pthread_mutex_lock(&mutex_lock);
		if (MESSAGE_RECEIVED == 2) {	// GENERATE PACKET
			MESSAGE_RECEIVED = 0;
			generate_data_packet(CLIENT_ARRAY[0], CLIENT_ARRAY[1], node, node_sock); // called when received
			memset(CLIENT_ARRAY, -1, sizeof(CLIENT_ARRAY));
		}
		pthread_mutex_unlock(&mutex_lock);
		
		FD_ZERO(&rfds);
		FD_SET(node_sock, &rfds);
	}
}

/*--------------------------------------------------------------------*/

Node* parse_file(char * filename, int current_ID) {

	int control_port, data_port, node_ID, neighbor;
	string hostname, line;
	vector<int> neighbor_ids;

	Node * node = NULL;

	ifstream input_file(filename);
	if (!input_file.good()) {
		perror("Error opening file");
		exit(1);
	}

	while(getline(input_file, line)) {
		istringstream ss(line);
		ss >> node_ID >> hostname >> control_port >> data_port;
		if(node_ID == current_ID){
			while (ss >> neighbor) {
				neighbor_ids.push_back(neighbor);
			}
			node = new Node();
			node->id = node_ID;
			char * temp_char = new char[hostname.length()+1];
			strcpy(temp_char, hostname.c_str());
			node->hostname = temp_char;
			node->control_port = control_port;
			node->data_port = data_port;
		}
	}
	input_file.close();

	if(neighbor_ids.size() > 0){
		ifstream file_second(filename);
		if (!file_second.good()) {
			perror("Error opening file second time");
			exit(1);
		}
		while(getline(file_second, line)) {
			istringstream ss(line);
			ss >> node_ID;
			for(int i : neighbor_ids){
				if(node_ID == i){
					ss >> hostname >> control_port >> data_port;
					Neighbor n1;
					n1.node_id = i;
					n1.control_port = control_port;
					n1.data_port = data_port;
					char * temp_char = new char[hostname.length()+1];
					strcpy(temp_char, hostname.c_str());
					n1.host_name = temp_char;
					node->neighbors.push_back(n1);
				}
			}
		}
		file_second.close();
	}
 	return node;
}

int main(int argc, char * argv[]){
	MESSAGE_RECEIVED = 0;
	PACKET = 0;
	pthread_mutex_init(&mutex_lock, NULL);

	if (argc == 3) {
		
		int node_ID = atoi(argv[2]);
		Node * node = parse_file(argv[1], node_ID);
		INPUT_FILENAME = argv[1];
		
		printf("%d ", node->id);
		cout << node->hostname << " " << node->control_port << " " << node->data_port << " " << endl;
		for(Neighbor n : node->neighbors){
			printf("(%d ", n.node_id); cout << " " << n.host_name << " " << n.control_port << " " << n.data_port << ")"  << endl;
		}

		// CONTROL THREAD
		pthread_t control_thread;
		if (pthread_create(&control_thread, NULL, control, (void*) node)) {
			perror("pthread_create");
			exit(1);
		}

		// DATA THREAD
		pthread_t data_thread;
		if (pthread_create(&data_thread, NULL, data, (void*) node)) {
			perror("pthread_create");
			exit(1);
		}
	
		pthread_join(control_thread, NULL);
		pthread_join(data_thread, NULL);

	}
	else {
		cout << "Usage: ./routing <input_file> <node_ID>" << endl;
		exit(1);
	}
	return 0;
}
