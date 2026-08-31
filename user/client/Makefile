CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -I../../include

CLIENT_BIN = client.o
CLIENT_SRC = client.cpp

all: deamon

deamon: $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC)

clean:
	rm -f $(CLIENT_BIN)
