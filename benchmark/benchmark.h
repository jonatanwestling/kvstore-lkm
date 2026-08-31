#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <string>

#define PORT 9999
#define DAEMON_IP_LOCAL "127.0.0.1"
#define USERNAME_DEFAULT "vboxuser"
#define PASSWORD_DEFAULT "vboxuser"


struct connection_data
{
    std::string username;
    std::string password;
    std::string daemon_ip;
};

// Benchmarks
void run_bench(int id, struct connection_data conn_data);
void run_baseline_single_thread(struct connection_data conn_data);
void run_concurrent_read_write(struct connection_data conn_data);
void run_latency_bench_single_core(struct connection_data conn_data);
void run_latency_bench_multi_thread(struct connection_data conn_data);
bool check_value(const std::string &resp, long long &out);

// Setup connection and authenticate
int setup_connection(const char *daemon_ip, const std::string &username, const std::string &password);
int auth_user(int sock, const std::string &username, const std::string &password);
std::string get_password();

// KV operations
int kv_insert(int sock, const std::string &key, const std::string &value);
std::string kv_get(int sock, const std::string &key);
int kv_delete(int sock, const std::string &key);
int kv_update(int sock, const std::string &key, const std::string &value);

#endif
