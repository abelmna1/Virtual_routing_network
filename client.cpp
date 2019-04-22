/*--------------------------------------------------------------------*/
/* Client program */

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include "route_utils.hpp"

#define MAX_SIZE 1024

using namespace std;

typedef struct {
	char id;
	char * hostname;
	int control_port;
} Client_Neighbor;

typedef struct {
	char * hostname;
	int control_port;
	vector<Client_Neighbor> neighbors;
} Client;

Client* parse_file(char * filename) {

	int control_port, data_port, node_ID, input_neighbor;
	vector<int> neighbor_ids;
	string hostname, line;

	Client * client = new Client();

	ifstream input_file(filename);
	if (!input_file.good()) {
		perror("Error opening file");
		exit(1);
	}

	while(getline(input_file, line)) {
		istringstream ss(line);	
		ss >> node_ID >> hostname >> control_port;
		Client_Neighbor neighbor;
		neighbor.id = node_ID;
		char * temp_char = new char[hostname.length()+1];
		strcpy(temp_char, hostname.c_str());
		neighbor.hostname = temp_char;
		neighbor.control_port = control_port;
		client->neighbors.push_back(neighbor);	
	}
	input_file.close();

 	return client;
}

// IDENTIFIER 2 = NEW PACKET
// sends a message to source_node to generate a packet destined to destination_node

// IDENTIFIER 3 = NEW LINK
// sends a message to the node indicating a new link has been established
// nodes will exchange distance vectors and update their tables accordingly

// IDENTIFIER 4 = REMOVE LINK
// sends a message to the node indicating a link has been removed
// node updates its routing table and sends vectors to its neighbors

void link_notification(int source_node, int destination_node, int sock, Client* client, int identifier) {
	char link_message[MAX_SIZE];
	int found = 0;
	int destination_found = 0;
	int remove_link_set = 0;
	
	if (identifier == 2) { // NEW PACKET
		memset(link_message, -2, MAX_SIZE);
	}
	else if (identifier == 3) { // NEW LINK
		memset(link_message, -3, MAX_SIZE);
	}
	else { // REMOVE LINK
		memset(link_message, -4, MAX_SIZE);
		remove_link_set = 1;
	}

	if(remove_link_set == 0){
		Control_Header header;
		header.source_node_id = source_node;
		header.dest_node_id = destination_node;
		memcpy(link_message, &header, sizeof(Control_Header));
	
		struct sockaddr_in node_addr;
		memset((char*) &node_addr, 0, sizeof(node_addr));
		struct hostent * host_entry;

		node_addr.sin_family = AF_INET;
		for(int j = 0; j < client->neighbors.size(); j++) {
			if(client->neighbors[j].id == source_node) {
				node_addr.sin_port = htons(client->neighbors[j].control_port);	
				host_entry = gethostbyname(client->neighbors[j].hostname);
				if(!host_entry){
					perror("gethostbyname: new link");
					exit(1);
				}
				found = 1;
				break;
			} 
		}
	
		if(found == 1){
			found = 0;
			for(int j = 0; j < client->neighbors.size(); j++){
				if(destination_node == client->neighbors[j].id){
					found = 1;
					destination_found = 1;
					break;
				}
			}
			if(found == 1){
				memcpy(&node_addr.sin_addr, host_entry->h_addr, host_entry->h_length);
				if(sendto(sock, link_message, (sizeof(Control_Header)+sizeof(Control_Payload)), 0, (struct sockaddr *) &node_addr, sizeof(node_addr)) == -1){
					perror("sendto");
					exit(1);
				}
			}
		}
		if(found == 0 || destination_found == 0) cout << "Node ID not recognized" << endl;
	}
	else{
		Control_Header header;
		int removed_found = 0;
		header.source_node_id = source_node;
		header.dest_node_id = destination_node;
		for(int j = 0; j < client->neighbors.size(); j++){
			if(client->neighbors[j].id == destination_node || client->neighbors[j].id == source_node){
				removed_found = 1;
			}
		}
		if(removed_found == 0){
			cout << "Node ID not recognized" << endl;
			return;
		}  

		memcpy(link_message, &header, sizeof(Control_Header));
	
		struct sockaddr_in node_addr;
		
		struct hostent * host_entry;

		for(int j = 0; j < client->neighbors.size(); j++) {
			memset((char*) &node_addr, 0, sizeof(node_addr));
			node_addr.sin_family = AF_INET;			
			node_addr.sin_port = htons(client->neighbors[j].control_port);	
			host_entry = gethostbyname(client->neighbors[j].hostname);
			if(!host_entry){
				perror("gethostbyname: remove link");
				exit(1);
			}
			memcpy(&node_addr.sin_addr, host_entry->h_addr, host_entry->h_length);
			if(sendto(sock, link_message, (sizeof(Control_Header)+sizeof(Control_Payload)), 0, (struct sockaddr *) &node_addr, sizeof(node_addr)) == -1){
				perror("sendto");
				exit(1);
			}
		}
	}

}

int main(int argc, char * argv[]){

	if (argc == 2) {
		
		int argument_flag = 0;
	
		Client * client = parse_file(argv[1]);	

		char client_host[MAX_SIZE];
		if (gethostname(client_host, sizeof(client_host)) == -1) {
			perror("gethostname");
			exit(1);
		}
		client->hostname = client_host;
		int client_sock = start_router(-2, client->hostname);

		cout << "Usage: <command> <node_1> <node_2>" << endl;
		cout << "Command options: 'generate-packet', 'create-link', 'remove-link'" << endl;

		while (1) {

			cout << ">> ";

			char * command = NULL;
			char * line = NULL;
			char * user_input[3];
			int node_1, node_2, i = 0;
			size_t len = 0;
			
			getline(&line, &len, stdin);
			command = strtok(line, " ");
			
			while (line != NULL) {
				if (i < 3) {
					user_input[i] = line;
					line = strtok(NULL, " ");
					i++;
				}
				else { // TOO MANY ARGUMENTS
					argument_flag = 1;
					break;
				}
			}

			if (i < 3 || argument_flag == 1) { // TOO FEW ARGUMENTS
				argument_flag = 0;
				cout << "Usage: <command> <node_1> <node_2>" << endl;
				cout << "Command options: 'generate-packet', 'create-link', 'remove-link'" << endl;
				continue;
			}

			command = user_input[0];
			node_1 = atoi(user_input[1]);
			node_2 = atoi(user_input[2]);

			if (strcmp(command, "generate-packet") == 0) {
				link_notification(node_1, node_2, client_sock, client, 2);
			}
			else if (strcmp(command, "create-link") == 0) {
				link_notification(node_1, node_2, client_sock, client, 3);
				link_notification(node_2, node_1, client_sock, client, 3);
			}
			else if (strcmp(command, "remove-link") == 0) {
				link_notification(node_1, node_2, client_sock, client, 4);
				link_notification(node_2, node_1, client_sock, client, 4);
			}
			else { // INVALID COMMAND
				cout << "Usage: <command> <node_1> <node_2>" << endl;
				cout << "Command options: 'generate-packet', 'create-link', 'remove-link'" << endl;
				continue;
			}
		}
	}
	else {
		cout << "Usage: ./client <input_file>" << endl;
		exit(1);
	}

	return 0;
}
