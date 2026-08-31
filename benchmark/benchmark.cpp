#include <termios.h>
#include <unistd.h>
#include <cstring>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>


#include <cstdio>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <thread>
#include <atomic>

#include "benchmark.h"


int main(void)
{
    std::string line;
    struct connection_data conn_data;

    std::cout << "Enter ip-address for server (empty for localhost): ";
    std::getline(std::cin, conn_data.daemon_ip);
    if (conn_data.daemon_ip.empty()) {
        conn_data.daemon_ip = DAEMON_IP_LOCAL;
    }

    std::cout << "Enter username: (empty for default 'vboxuser') ";
    std::getline(std::cin, conn_data.username);
    if (conn_data.username.empty()) {
        conn_data.username = USERNAME_DEFAULT;
    }

    std::cout << "Enter password: (empty for default 'vboxuser') ";
    conn_data.password = get_password();
    if (conn_data.password.empty()) {
        conn_data.password = PASSWORD_DEFAULT;
    }

    while (true)
    {
        std::cout << "bench> ";
        std::getline(std::cin, line);

        if (line == "exit")
        {
            break;
        }
        else if (line == "list")
        {
            std::cout << "List of bencmarks: \n";
            std::cout << "1. Baseline (Single Thread)\n";
            std::cout << "2. Concurrent Read/Write\n";
            std::cout << "3. Latency Benchmark (Single Core)\n";
            std::cout << "4. Latency Benchmark (Multi Thread)\n";
            std::cout << "Type 'run <id>' to run a specific benchmark, or 'run all' to run all benchmarks\n";

        }
        else if (line == "run all")
        {
            run_bench(1, conn_data);
            run_bench(2, conn_data);
            run_bench(3, conn_data);
            run_bench(4, conn_data);
        }
        else if (line.rfind("run ", 0) == 0) {
            int id = std::stoi(line.substr(4));
            run_bench(id, conn_data);
        }
    }
}

//Benchmarks
void run_bench(int id, struct connection_data conn_data)
{
    switch (id)
    {
    case 1:
        run_baseline_single_thread(conn_data);
        break;
    case 2:
        run_concurrent_read_write(conn_data);
        break;
    case 3:
        run_latency_bench_single_core(conn_data);
        break;
    case 4:
        run_latency_bench_multi_thread(conn_data);
        break;
    default:
        std::cerr << "Invalid benchmark ID\n";
        break;
    }
}

double measure_insert(int sock, const std::string &key, const std::string &value)
{
    auto start = std::chrono::steady_clock::now();
    kv_insert(sock, key, value);
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

double measure_update(int sock, const std::string &key, const std::string &value)
{
    auto start = std::chrono::steady_clock::now();
    kv_update(sock, key, value);
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

double measure_lookup(int sock, const std::string &key, std::string &out)
{
    auto start = std::chrono::steady_clock::now();
    out = kv_get(sock, key);
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

double measure_delete(int sock, const std::string &key)
{
    auto start = std::chrono::steady_clock::now();
    kv_delete(sock, key);
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

void run_latency_bench_single_core(struct connection_data conn_data)
{
    std::cout << "Running latency_bench_single_core benchmark...\n";

    int sock = setup_connection(conn_data.daemon_ip.c_str(), conn_data.username, conn_data.password);
    if (sock < 0) 
    {
        std::cerr << "Failed to setup connection for latency_bench_single_core benchmark\n";
        return;
    }

    const int key_count = 20000;
    const int warmup_count = 2000;

    std::vector<std::string> keys;
    keys.reserve(key_count);

    for (int i = 0; i < key_count; i++)
    {
        keys.push_back(std::to_string(i));
    }

    std::ofstream file("latency_bench_single_core.csv");
    
    file << "operation, time_us\n";

    // Warmup
    for (int i = 0; i < warmup_count; ++i)
    {
        kv_insert(sock, std::to_string((key_count * 10) + i), "Warmup:" + std::to_string(i));
    }

    // Benchmark KV_INSERT
    for (int i = 0; i < key_count; ++i)
    {
        double latency = measure_insert(sock, keys[i], "value" + std::to_string(i));
        file << "insert, " << latency << "\n";
    }

    // Benchmark KV_LOOKUP
    std::string out;
    for (int i = 0; i < key_count; i++)
    {
        double latency = measure_lookup(sock, keys[i], out);
        file << "lookup, " << latency << "\n";
    }

    // Benchmark KV_UPDATE
    for (int i = 0; i < key_count; i++)
    {
        double latency = measure_update(sock, keys[i], "updated_value" + std::to_string(i));
        file << "update, " << latency << "\n";
    }

    // Benchmark KV_DELETE
    for (int i = 0; i < key_count; i++)
    {
        double latency = measure_delete(sock, keys[i]);
        file << "delete, " << latency << "\n";
    }
    file.close();
    close(sock);

    std::cout << "Latency benchmark finished. Results saved to latency_bench_single_core.csv\n";
}

void run_latency_bench_multi_thread(struct connection_data conn_data)
{
    std::cout << "Running latency_bench_multi_thread benchmark...\n";

    const int thread_count = 8;
    const int key_count = 20000;
    const int warmup_count = 2000;

    auto worker = [&](int tid)
    {
        int sock = setup_connection(conn_data.daemon_ip.c_str(), conn_data.username, conn_data.password);
        if (sock < 0)
        {
            std::cerr << "Thread " << tid << " failed to setup connection\n";
            return;
        }

        std::vector<std::string> keys;
        keys.reserve(key_count);

        int base_key = tid * 1000000;

        for (int i = 0; i < key_count; i++)
        {
            keys.push_back(std::to_string(base_key + i));
        }

        std::string filename = "latency_bench_multi_thread_" + std::to_string(tid) + ".csv";
        std::ofstream file(filename);

        if (!file.is_open())
        {
            std::cerr << "Thread " << tid << " failed to open file " << filename << "\n";
            close(sock);
            return;
        }

        file << "operation,time_us\n";

        // Warmup
        for (int i = 0; i < warmup_count; ++i)
        {
            kv_insert(sock, std::to_string(base_key + (key_count * 10) + i), "Warmup:" + std::to_string(i));
        }

        // INSERT
        for (int i = 0; i < key_count; ++i)
        {
            double latency = measure_insert(sock, keys[i], "value" + std::to_string(i));
            file << "insert," << latency << "\n";
        }

        // LOOKUP
        std::string out;
        for (int i = 0; i < key_count; i++)
        {
            double latency = measure_lookup(sock, keys[i], out);
            file << "lookup," << latency << "\n";
        }

        // UPDATE
        for (int i = 0; i < key_count; i++)
        {
            double latency = measure_update(sock, keys[i], "updated_value" + std::to_string(i));
            file << "update," << latency << "\n";
        }

        // DELETE
        for (int i = 0; i < key_count; i++)
        {
            double latency = measure_delete(sock, keys[i]);
            file << "delete," << latency << "\n";
        }

        file.close();
        close(sock);

        std::cout << "Thread " << tid << " finished. Results saved to " << filename << "\n";
    };

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int i = 0; i < thread_count; ++i)
    {
        threads.emplace_back(worker, i);
    }

    for (auto &t : threads)
    {
        t.join();
    }

    std::cout << "Multi-thread latency benchmark finished\n";
}


void run_baseline_single_thread(struct connection_data conn_data)
{
    std::cout << "Running baseline_single_thread benchmark...\n";

    int sock = setup_connection(conn_data.daemon_ip.c_str(), conn_data.username, conn_data.password);
    if (sock < 0) 
    {
        std::cerr << "Failed to setup connection for baseline_single_thread benchmark\n";
        return;
    }

    int key_count = 50000;
    std::vector<std::string> keys;
    keys.reserve(key_count);

    int failed_inserts = 0;
    int failed_get_empty = 0;
    int failed_get_mismatch = 0;
    int failed_deletes = 0;

    for (int i = 0; i < key_count; i++)
        keys.push_back(std::to_string(i));

    auto start = std::chrono::steady_clock::now();

    // INSERT
    for (int i = 0; i < key_count; ++i)
    {
        const std::string &k = keys[i];
        std::string value = "value" + std::to_string(i);
        int res = kv_insert(sock, k, value);
        if (res < 0) 
        {
            failed_inserts++;
        }
    }
    // GET
    std::string out;
    for (int i = 0; i < key_count; ++i)
    {
        const std::string &k = keys[i];
        std::string value = "value" + std::to_string(i);
        out = kv_get(sock, k);
        if (out.empty()) 
        {
            failed_get_empty++;
        }
        else if (out != value && out.find("\"" + value + "\"") == std::string::npos)
        {
            failed_get_mismatch++;
            std::cout << "GET mismatch for key " << k << ": expected '" << value << "', got '" << out << "'\n";
        }
    }

    // DELETE
    for (const auto& k : keys)
    {
       int res = kv_delete(sock, k);
       if (res < 0) 
       {
            failed_deletes++;
       }
    }

    auto end = std::chrono::steady_clock::now();

    close(sock);

    double seconds = std::chrono::duration<double>(end - start).count();

    int total_ops = key_count * 3;

    std::cout << "Baseline benchmark finished\n";
    std::cout << "Operations: " << total_ops << "\n";
    std::cout << "Time: " << seconds << " s\n";
    std::cout << "Ops/sec: " << total_ops / seconds << "\n";
    std::cout << "Failed inserts: " << failed_inserts << "\n";
    std::cout << "Failed gets (empty): " << failed_get_empty << "\n";
    std::cout << "Failed gets (mismatch): " << failed_get_mismatch << "\n";
    std::cout << "Failed deletes: " << failed_deletes << "\n";
}

void run_concurrent_read_write(struct connection_data conn_data)
{
    std::cout << "Running concurrent_read_write benchmark...\n";

    const int total_threads = 10;
    const int reader_threads = total_threads - 1;
    const int duration_sec = 60;

    const std::string key = "0";
    
    int sock = setup_connection(conn_data.daemon_ip.c_str(), conn_data.username, conn_data.password);
    if (sock <= 0) 
    {
        std::cerr << "Failed to connect for init\n";
        return;
    }

    kv_insert(sock, key, "count:0");
    close(sock);

    std::atomic<bool> stop{false};
    std::atomic<unsigned long long> writes{0};
    std::atomic<unsigned long long> reads{0};
    std::atomic<unsigned long long> empty_reads{0};
    std::atomic<unsigned long long> bad_reads{0};

    // Writer thread: updates key with increasing counter
    auto writer = [&]() 
    {
        int sock = setup_connection(conn_data.daemon_ip.c_str(), conn_data.username, conn_data.password);
        if (sock <= 0) 
        {
            std::cerr << "Writer failed to connect\n";
            return;
        }

        unsigned long long count = 0;
        while (!stop.load(std::memory_order_relaxed)) 
        {
            kv_update(sock, key, "count:" + std::to_string(count++));
            writes.fetch_add(1, std::memory_order_relaxed);
        }

        close(sock);
    };

    // Reader threads: GET and validate it gets the correct value
    auto reader = [&](int tid) 
    {
        (void)tid;
        int sock = setup_connection(conn_data.daemon_ip.c_str(), conn_data.username, conn_data.password);
        if (sock <= 0) 
        {
            std::cerr << "Reader failed to connect\n";
            return;
        }

        while (!stop.load(std::memory_order_relaxed)) 
        {
            std::string resp = kv_get(sock, key);
            reads.fetch_add(1, std::memory_order_relaxed);

            if (resp.empty()) 
            {
                empty_reads.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            long long val = 0;
            if (!check_value(resp, val)) 
            {
                bad_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }

        close(sock);
    };

    std::vector<std::thread> threads;
    threads.reserve(total_threads);

    auto start = std::chrono::steady_clock::now();

    threads.emplace_back(writer);
    for (int i = 0; i < reader_threads; ++i)
        threads.emplace_back(reader, i);

    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    stop.store(true, std::memory_order_relaxed);

    for (auto &t : threads) t.join();

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "writer_readers_hotkey finished\n";
    std::cout << "Key: " << key << "\n";
    std::cout << "Threads: " << total_threads << " (1 writer, " << reader_threads << " readers)\n";
    std::cout << "Duration: " << seconds << " s\n";
    std::cout << "Writes: " << writes.load() << " (" << (writes.load() / seconds) << " ops/sec)\n";
    std::cout << "Reads: " << reads.load() << " (" << (reads.load() / seconds) << " ops/sec)\n";
    std::cout << "Empty reads: " << empty_reads.load() << "\n";
    std::cout << "Bad reads: " << bad_reads.load() << "\n";
    std::cout << "Total ops/sec: " << ((writes.load() + reads.load()) / seconds) << "\n";
}


bool check_value(const std::string &resp, long long &out)
{
    std::size_t pos = resp.find("count:");
    if (pos == std::string::npos) return false;
    pos += 6;

    // skip quotes/spaces
    while (pos < resp.size() && (resp[pos] == '"' || std::isspace((unsigned char)resp[pos])))
        pos++;

    if (pos >= resp.size() || !std::isdigit((unsigned char)resp[pos]))
        return false;

    long long v = 0;
    while (pos < resp.size() && std::isdigit((unsigned char)resp[pos])) {
        v = v * 10 + (resp[pos] - '0');
        pos++;
    }

    out = v;
    return true;
}

// Setup connection and authenticate
int setup_connection(const char *daemon_ip, const std::string &username, const std::string &password) 
{

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) 
    {
        perror("failed to create socket\n");
        return -1;
    }

    // server
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // convert to binary form
    int r = inet_pton(AF_INET, daemon_ip, &server.sin_addr);
    if (r != 1) 
    {
        fprintf(stderr, "Invalid IPv4 address: %s\n", daemon_ip);
        return -1;
    }
    if (connect(sock, (sockaddr *)&server, sizeof(server)) < 0) 
    {
        perror("could not connect to server\n");
        return -1;
    }

    auth_user(sock, username, password);

    return sock;
}


int auth_user(int sock, const std::string &username, const std::string &password) {
        std::string msg = "AUTH " + username + " " + password;

        // send to daemon
        if (send(sock, msg.c_str(), msg.size(), 0) < 0) {
            perror("failed to send auth");
            return -1;
        }

        char buffer[128] = {0};
        int n = read(sock, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            std::cerr << "auth response failed\n";
            return -1;
        }

        std::string resp(buffer);
        if (resp.find("OK") != std::string::npos) {
            std::cout << "\n";
            std::cout << "Authentication success\n";
            return 0;
        } else {
            std::cout << "Authentication failed, shutting down...\n";
            return -1;
        }
}

std::string get_password() 
{
    termios oldt, newt;
    std::string password;
    // Get current terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    // Disable echo
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    getline(std::cin, password);

    // Restore to previous settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    std::cout << std::endl;

    return password;
}


// KV operations
int kv_insert(int sock, const std::string &key, const std::string &value) 
{
    const std::string command = "KV_INSERT " + key + " " + value;

    // Send command to server
    send(sock, command.c_str(), command.size(), 0);

    char buffer[1024] = {0};

    // Read response from server
    int n = read(sock, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';
    } 
    else if (n == 0) {
        std::cout << "Server closed connection\n";
        return -1;
    } 
    else {
        perror("read failed");
        return -1;
    }

    return 0;
}

std::string kv_get(int sock, const std::string &key) 
{
    const std::string command = "KV_GET " + key;

    // Send command to server
    send(sock, command.c_str(), command.size(), 0);

    char buffer[1024] = {0};

    // Read response from server
    int n = read(sock, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';
        return std::string(buffer);
    } 
    else if (n == 0) {
        std::cout << "Server closed connection\n";
        return "";
    } 
    else {
        perror("read failed");
        return "";
    }
}

int kv_delete(int sock, const std::string &key) 
{
    const std::string command = "KV_DELETE " + key;

    // Send command to server
    send(sock, command.c_str(), command.size(), 0);

    char buffer[1024] = {0};

    // Read response from server
    int n = read(sock, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';
        return 0;
    } 
    else if (n == 0) {
        std::cout << "Server closed connection\n";
        return -1;
    } 
    else {
        perror("read failed");
        return -1;
    }
}

int kv_update(int sock, const std::string &key, const std::string &value) 
{
    const std::string command = "KV_UPDATE " + key + " " + value;

    // Send command to server
    send(sock, command.c_str(), command.size(), 0);

    char buffer[1024] = {0};

    // Read response from server
    int n = read(sock, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';
        return 0;
    } 
    else if (n == 0) {
        std::cout << "Server closed connection\n";
        return -1;
    } 
    else {
        perror("read failed");
        return -1;
    }
}