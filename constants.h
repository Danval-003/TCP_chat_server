#ifndef CONSTANTS_H
#define CONSTANTS_H

constexpr const char* SERVER_IP = "127.0.0.1";
constexpr int PORT = 4000;

// Set the maximum buffer (message) size to 5000 bytes.
// This limit is predetermined to ensure sufficient space for data processing,
// preventing buffer overflow and maintaining system stability.
constexpr int BUFFER_SIZE = 5000;

constexpr const char* TEST_USERNAME = "agony_";

#endif