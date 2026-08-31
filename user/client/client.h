#ifndef CLIENT_H
#define CLIENT_H


#include <cstdio>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <termios.h>
#include <unistd.h>

#define PORT 9999
#define DAEMON_IP_LOCAL "127.0.0.1"

void send_command(int sock, const std::string& command);
void cli_menu(int sock);
int auth_user(int sock);
std::string get_password();

#endif /* CLIENT_H */
