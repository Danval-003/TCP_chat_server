#ifndef CONSTANTS_H
#define CONSTANTS_H

constexpr const char* SERVER_IP = "3.141.42.86";
constexpr int PORT = 4000;

// Set the maximum buffer (message) size to 5000 bytes.
// This limit is predetermined to ensure sufficient space for data processing,
// preventing buffer overflow and maintaining system stability.
constexpr int BUFFER_SIZE = 5000;

constexpr const char* TEST_USERNAME = "ggg";

constexpr const char* CYAN = "\033[96m";
constexpr const char* GREEN = "\033[92m";
constexpr const char* RED = "\033[91m";
constexpr const char* YELLOW = "\033[93m";
constexpr const char* WHITE = "\033[97m";
constexpr const char* RESET = "\033[0m";
constexpr const char* BOLD = "\033[1m";
constexpr const char* UNDERLINE = "\033[4m";
constexpr const char* REVERSE = "\033[7m";

#endif