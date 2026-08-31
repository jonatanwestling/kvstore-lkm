#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>

#include "auth.h"
#include "daemon.h"
#include "kv_ioctl.h"

static volatile sig_atomic_t shutdown_requested = 0;
static int shutdown_server_fd = -1;

static bool parse_key(const std::string &key_text, unsigned long long *out_key) {
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(key_text.c_str(), &end, 10);
    if (errno != 0 || end == key_text.c_str() || *end != '\0') {
        return false;
    }
    *out_key = parsed;
    return true;
}

/**
 * Signal handeler that handle SIGINT and SIGTERM to shutdown the server
 * and store the kv entries to file before exiting by setting shutdown_requested
 * flag and closing the server socket to unblock accept()
 */
static void handle_shutdown_signal(int signal_number) {
    (void)signal_number;
    shutdown_requested = 1;
    if (shutdown_server_fd >= 0) {
        close(shutdown_server_fd);
    }
}

int main() {
    // Register signal handlers for shutdown
    signal(SIGINT, handle_shutdown_signal);
    signal(SIGTERM, handle_shutdown_signal);

    int kv_fd = init_kv();
    if (kv_fd <= 0) {
        return EXIT_FAILURE;
    }

    do_kv_restore_from_file(kv_fd);

    // Initialize network and start listening for clients
    int server_fd = init_network();
    if (server_fd < 0) {
        kill_kv(kv_fd);
        return EXIT_FAILURE;
    }
    // Store server_fd in global variable for signal handler
    shutdown_server_fd = server_fd;

    // Start accepting clients and handling requests
    listen_network(server_fd, kv_fd);

    // Server is shutting down, snapshot kv entries to file and clean up
    do_kv_snapshot_to_file(kv_fd);
    kill_network(server_fd);
    kill_kv(kv_fd);
    return EXIT_SUCCESS;
}

/**
 * Init the kv store by opening the device file and return the file descriptor
 */
int init_kv() {
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }
    return fd;
}

/**
 * Close the kv store file descriptor
 */
void kill_kv(int fd) { close(fd); }

/**
 * Init the network by creating a server socket, binding to the specified port,
 * and start listening Returns the server socket file descriptor on success, -1
 * on failure
 */
int init_network() {
    int server_fd;
    sockaddr_in server_address{};
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Failed to create socket\n";
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
        std::cerr << "Failed to set socket options\n";
        close(server_fd);
        return -1;
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr *)&server_address, sizeof(server_address)) <
        0) {
        std::cerr << "Failed to bind socket\n";
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        std::cerr << "Failed to listen on socket\n";
        close(server_fd);
        return -1;
    }
    std::cout << "Server is listening on port " << PORT << "...\n";

    return server_fd;
}

/**
 * Handle incoming client connections and loop until shutdown is requested.
 * For each accepted client connection, a new thread will be used to handle the
 * client's requests.
 */
void listen_network(int server_fd, int kv_fd) {
    while (!shutdown_requested) {
        sockaddr_in client_address{};
        socklen_t client_address_len = sizeof(client_address);

        int client_fd =
            accept(server_fd, (sockaddr *)&client_address, &client_address_len);
        if (client_fd < 0) {
            if (shutdown_requested || errno == EINTR) {
                continue;
            }
            std::cerr << "Failed to accept connection\n";
            continue;
        }

        std::thread(handle_client, client_fd, kv_fd).detach();
    }
}

/**
 * Handle a single client's requests in a loop until the client disconnects or
 * an error occurs. For each received command, it will be parsed and the
 * corresponding kv operation will be performed by calling do_kv_operation() or
 * do_kv_snapshot_to_file() for snapshot command.
 */
static void handle_client(int client_fd, int kv_fd) {
    char buffer[1024];
    bool authenticated = false;

    while (true) {
        std::memset(buffer, 0, sizeof(buffer));

        int n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n == 0) {
            std::cerr << "Client disconnected\n";
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Failed to read from client\n";
            break;
        }

        std::cout << "Received from client: " << buffer << "\n";

        std::string response = parse_and_handle_command(buffer, kv_fd,  authenticated);

        if (write(client_fd, response.c_str(), response.size()) < 0) {
            std::cerr << "Failed to write to client\n";
            break;
        }
    }

    close(client_fd);
}

/**
 * This function parses the client's command and calls the appropriate handler
 * function based on the operation specified in the command.
 */
std::string parse_and_handle_command(const std::string &command, int kv_fd, bool &authenticated) {
    std::istringstream iss(command);
    std::string operation, key, value;

    // Confirm the command structure
    if (!(iss >> operation)) {
        std::cerr << "Invalid command format. Expected: <operation> ...\n";
        return "Invalid command format. Expected: kv <operation> ...\n";
    }
    // Handle auth
    if (operation == "AUTH") {
        std::string username, password;
        if (!(iss >> username)) {
            return "No username provided";
        }
        if (!(iss >> password)) {
            return "No password provided";
        }
        bool ok = authenticate(username.c_str(), password.c_str());
        if (ok) {
            authenticated = true;
            return "OK\n";
        } else {
            return "FAIL\n";
        }
    }

    // Check if the user on the socket has authenticated
    if (!authenticated){
        return "Connection not authenticated!\n";
    }

    // Manual trigger for kv snapshot
    if (operation == "KV_SNAPSHOT") {
        do_kv_snapshot_to_file(kv_fd);
        return "Snap";
    }


    // All other operations require a key
    if (!(iss >> key)) {
        std::cerr << "Invalid command format. Expected: kv <operation> <key> ";
        return "Invalid command format. Expected: kv <operation> <key> ";
    }

    // Read the rest of the line as value (if any)
    std::getline(iss, value);
    if (!value.empty() && value[0] == ' ') {
        value = value.substr(1);
    }

    unsigned long cmd = 0;
    // Determine the command type
    if (operation == "KV_INSERT") {
        if (value.empty()) {
            std::cerr << "KV_INSERT requires a value\n";
            return "KV_INSERT requires a value\n";
        }
        cmd = KV_INSERT;
    } else if (operation == "KV_DELETE") {
        cmd = KV_DELETE;
    } else if (operation == "KV_GET") {
        cmd = KV_GET;
    } else if (operation == "KV_UPDATE") {
        if (value.empty()) {
            std::cerr << "KV_UPDATE requires a value\n";
            return "KV_UPDATE requires a value\n";
        }
        cmd = KV_UPDATE;
    } else {
        std::cerr << "Unknown operation: " << operation << "\n";
        return "Unknown operation: " + operation + "\n";
    }
    // Now perform the acctual operation.
    return do_kv_operation(kv_fd, cmd, key, value);
}

/**
 * This function saves all the current key-value entries in the kv store
 * snapshot to a file in the userspace.
 */
void do_kv_snapshot_to_file(int fd) {
    unsigned int entry_count = 0;

    // Get the count of entries
    int ret = ioctl(fd, KV_COUNT, &entry_count);
    if (ret < 0) {
        perror("ioctl KV_COUNT failed");
        return;
    }

    if (entry_count == 0) {
        std::cout << "No entries to snapshot\n";
        return;
    }

    // Allocate the size needed for the entries
    size_t alloc_size =
        sizeof(kv_snapshot_message) + (entry_count * sizeof(kv_message));
    kv_snapshot_message *snapshot =
        (kv_snapshot_message *)calloc(1, alloc_size);
    if (!snapshot) {
        std::cerr << "Failed to allocate memory for " << entry_count
                  << " entries\n";
        return;
    }
    snapshot->capacity = entry_count;

    // Now we can get the entries
    ret = ioctl(fd, KV_SNAPSHOT, snapshot);
    if (ret < 0) {
        perror("ioctl KV_SNAPSHOT failed");
        free(snapshot);
        return;
    }

    
    size_t snapshot_size = sizeof(kv_snapshot_message) + snapshot->count * sizeof(kv_message);
    std::ofstream out(SNAPSHOT_OUTPUT_PATH, std::ios::binary);
    if (!out)
    {
        std::cerr << "Failed to open output file: " << SNAPSHOT_OUTPUT_PATH
                  << "\n";
        free(snapshot);
        return;
    }

    out.write(reinterpret_cast<const char*>(snapshot), snapshot_size);
    out.close();

    // Print summary of the snapshot
    std::cout << "Snapshot saved with " << snapshot->count << " entries to "
              << SNAPSHOT_OUTPUT_PATH << "\n";
    // frre allocated memory for snapshot
    free(snapshot);
}

void do_kv_restore_from_file(int fd) {
    kv_snapshot_message header{};

    std::ifstream in(SNAPSHOT_OUTPUT_PATH, std::ios::binary);
    if (!in) {
        std::cout << "No startup snapshot found at " << SNAPSHOT_OUTPUT_PATH
                  << "\n";
        return;
    }

    in.read(reinterpret_cast<char*>(&header), sizeof(kv_snapshot_message));

    size_t snapshot_size = sizeof(kv_snapshot_message) + header.count * sizeof(kv_message);
    kv_snapshot_message* snapshot = (kv_snapshot_message*)std::malloc(snapshot_size);

    std::memcpy(snapshot, &header, sizeof(kv_snapshot_message));

    const size_t entries_bytes = static_cast<size_t>(header.count) * sizeof(kv_message);
    in.read(reinterpret_cast<char*>(snapshot->entries), static_cast<std::streamsize>(entries_bytes));

    unsigned int restored = 0;
    unsigned int skipped = 0;

    for (unsigned int i = 0; i < snapshot->count; i++)
    {
        kv_message entry = snapshot->entries[i];

        int ret = ioctl(fd, KV_INSERT, &entry);
        if (ret < 0 || header.status != 0) {
            skipped++;
            continue;
        }
        restored ++;
    }
    
    std::free(snapshot);
    std::cout << "Startup restore complete: restored=" << restored
              << ", skipped=" << skipped << "\n";

}

void kill_network(int server_fd) { close(server_fd); }

std::string do_kv_operation(int fd, unsigned long cmd, const std::string &key,
                            const std::string &value) {
    kv_message msg = {};
    unsigned long long parsed_key = 0;

    if (!parse_key(key, &parsed_key)) {
        return "Invalid key: expected unsigned integer\n";
    }

    msg.key = parsed_key;

    // Copy value only when needed
    if (!value.empty()) {
        strncpy(msg.value, value.c_str(), MAX_VALUE_LEN - 1);
        msg.value[MAX_VALUE_LEN - 1] = '\0';
    } else {
        msg.value[0] = '\0';
    }

    // Use ioctl instead of write
    int ret = ioctl(fd, cmd, &msg);
    if (ret < 0) {
        perror("ioctl failed");
        return "ioctl failed";
    }

    // Show result
    std::string key_str = std::to_string(msg.key);
    std::string value_str = msg.value;
    std::string response;

    switch (cmd) {
    case KV_GET:
        if (msg.status == 0) {
            response = "GET(\"" + key_str + "\") → \"" + value_str + "\"\n";
        } else {
            response = "GET(\"" + key_str + "\") failed: key does not exist\n";
        }
        break;

    case KV_INSERT:
        if (msg.status == 0) {
            response = "INSERT(\"" + key_str + "\") succeeded\n";
        } else if (msg.status == 1){
            response = "INSERT(\"" + key_str +
           "\") failed, key already exists, use update\n";
        } else {
            response = "INSERT(\"" + key_str +
                       "\") failed (status=" + std::to_string(msg.status) +
                       ")\n";
        }
        break;

    case KV_UPDATE:
        if (msg.status == 0) {
            response = "UPDATE(\"" + key_str + "\") succeeded\n";
        } else {
            response = "UPDATE(\"" + key_str +
                       "\") failed (status=" + std::to_string(msg.status) +
                       ")\n";
        }
        break;

    case KV_DELETE:
        if (msg.status == 0) {
            response = "DELETE(\"" + key_str + "\") succeeded\n";
        } else {
            response = "DELETE(\"" + key_str +
                       "\") failed: key not found or error (status=" +
                       std::to_string(msg.status) + ")\n";
        }
        break;

    default:
        response = "Unknown command: " + std::to_string(cmd) +
                   " (status=" + std::to_string(msg.status) + ")\n";
        break;
    }

    return response;
}
