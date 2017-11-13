Written by Adam Belmnahia & Lauren Pagano  (Binghamton University)

This program constructs a router network topology provided by an input file. Each router is represented by 
a different process, each having a direct communication link to all of its neighbors. UDP sockets are used for 
communication in order to simulate connectionless transmissions. A separate program - client - is used to issue commands 
to any router in the network. These commands include direct link construction and destruction, along with data packet 
forwarding. Each router has 2 threads to handle incoming messages from its control and data ports. Furthermore, each router
will periodically send its routing table (a set of vectors to each reachable node) to all other neighboring routers. After 
receiving a routing table, a router will then perform the distance vector routing algorithm and possibly update its own
set of vectors.
