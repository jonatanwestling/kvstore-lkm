#ifndef DAEMON_H
#define DAEMON_H
#include <string>
#include <cstring>
#include <unistd.h>

#define DEVICE_PATH "/dev/kv_store"
#define PORT 9999
#define SNAPSHOT_OUTPUT_PATH "/var/lib/kvstore/snapshot.bin"

int init_kv();
int init_network();
void listen_network(int server_fd, int kv_fd);
static void handle_client(int client_fd, int kv_fd);
std::string do_kv_operation(int fd, unsigned long cmd, const std::string& key, const std::string& value);
void do_kv_snapshot_to_file(int fd);
void do_kv_restore_from_file(int fd);
std::string parse_and_handle_command(const std::string& command, int kv_fd, bool &authenticated);
void kill_kv(int fd);
void kill_network(int server_fd);

#endif /* DAEMON_H */
