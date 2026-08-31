#include "client.h"

#include <termios.h>
#include <unistd.h>
using namespace std;

int main(int argc, char *argv[]) {

    const char *daemon_ip;
    if (argc == 1) {
        daemon_ip = DAEMON_IP_LOCAL;
    } else if (argc == 2) {
        daemon_ip = argv[1];
    } else {
        printf("Usage: %s [ip]\n", argv[0]);
        printf("If no IP is provided, localhost (127.0.0.1) is used.\n");
        return 1;
    }
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("failed to create socket\n");
        return 1;
    }

    // server
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // convert to binary form
    int r = inet_pton(AF_INET, daemon_ip, &server.sin_addr);
    if (r != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", daemon_ip);
        return 1;
    }
    if (connect(sock, (sockaddr *)&server, sizeof(server)) < 0) {
        perror("could not connect to server\n");
        return 1;
    }

    // auth user
    if (auth_user(sock) == 0) {
        // Enter the CLI loop
        cli_menu(sock);
    }

    return 0;
}

int auth_user(int sock) {
    while (true) {
        string username;
        string password;

            cout << "\033[1;37m";
            
        cout << "Username: ";
        getline(cin, username);

        cout << "Password: ";
                    cout << "\033[0m";

        password = get_password();

        string msg = "AUTH " + username + " " + password;

        // send to daemon
        if (send(sock, msg.c_str(), msg.size(), 0) < 0) {
            perror("failed to send auth");
            return -1;
        }

        char buffer[128] = {0};
        int n = read(sock, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            cerr << "auth response failed\n";
            return -1;
        }

        string resp(buffer);
        if (resp.find("OK") != string::npos) {
            cout << "\n";
            cout << "Authentication success\n";
            return 0;
        } else {
            cout << "Authentication failed\n";
        }
    }
}

string get_password() {
    termios oldt, newt;
    string password;
    // Get current terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    // Disable echo
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    getline(cin, password);

    // Restore to previous settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    cout << endl;

    return password;
}

void cli_menu(int sock) {

    string line;
    bool running = true;
    while (running) {
        cout << "\033[1;34m";
        cout << "\n==== KV STORE MENU ====\n";
        cout << "\033[0m";

        cout << "\033[1;37m";
        cout << "1) Insert\n";
        cout << "2) Update\n";
        cout << "3) Get\n";
        cout << "4) Delete\n";
        cout << "5) Snapshot\n";
        cout << "6) Exit\n";
        cout << "\033[0m";

        cout << "\n\033[1;33m";
        cout << "Choose: ";
        cout << "\033[0m";

        string choice;
        getline(cin, choice);

        if (choice == "6" || choice == "exit") {
            running = false;
        } else if (choice == "1") {
            string key, value;
            cout << "Key: ";
            getline(cin, key);
            cout << "Value: ";
            getline(cin, value);

            send_command(sock, "KV_INSERT " + key + " " + value);
        } else if (choice == "2") {
            string key, value;
            cout << "Key: ";
            getline(cin, key);
            cout << "Value: ";
            getline(cin, value);

            send_command(sock, "KV_UPDATE " + key + " " + value);
        } else if (choice == "3") {
            string key;
            cout << "Key: ";
            getline(cin, key);

            send_command(sock, "KV_GET " + key);
        } else if (choice == "4") {
            string key;
            cout << "Key: ";
            getline(cin, key);

            send_command(sock, "KV_DELETE " + key);
        } else if (choice == "5") {
            send_command(sock, "KV_SNAPSHOT");
        } else {
            cout << "\n";
            cout << "\033[1;37m";
            cout << "Invalid choice\n";
            cout << "\033[0m";
        }
    }
    return;
}

void send_command(int sock, const string &command) {
    // Send command to server
    send(sock, command.c_str(), command.size(), 0);

    char buffer[1024] = {0};

    // Read response from server
    int n = read(sock, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';

        cout << "\n";
        cout << "\033[1;37m";
        cout << buffer;
        cout << "\033[0m";
    } 
    else if (n == 0) {
        cout << "Server closed connection\n";
    } 
    else {
        perror("read failed");
    }}
