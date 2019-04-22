CXXFLAGS = -std=c++11

all: routing client

routing: routing.cpp route_utils.hpp
  g++ -pthread -o routing routing.cpp -g

client: client.cpp route_utils.hpp
  g++ -pthread -o client client.cpp -g

clean:
  rm -rf *.o routing client
