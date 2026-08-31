CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -I../../include
LDFLAGS = -lpam

DAEMON_BIN = daemon
DAEMON_SRC = daemon.cpp auth.cpp

all: daemon

daemon: $(DAEMON_SRC)
	$(CXX) $(CXXFLAGS) -o $(DAEMON_BIN) $(DAEMON_SRC) $(LDFLAGS)

setup:
	sudo mkdir -p /var/lib/kvstore
	sudo chown $(USER):$(USER) /var/lib/kvstore
	sudo chmod 755 /var/lib/kvstore

clean:
	rm -f $(DAEMON_BIN)
	rm -f kv_snapshot.bin
