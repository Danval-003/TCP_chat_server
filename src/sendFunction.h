#pragma once // Only include this header file once
#include <sys/socket.h>
#include "../proto/chat.pb.h"
#include <string>
#include "../constants.h"

// Define fuction to send request to server
int sendRequest(chat::Request* request, int clientSocket);
// Define function to send response to client
int sendResponse(chat::Response* response, int clientSocket);

// Define fuction to obtain request from client
int getRequest(chat::Request* request, int clientSocket);
// Define function to obtain response from server
int getResponse(chat::Response* response, int clientSocket);
