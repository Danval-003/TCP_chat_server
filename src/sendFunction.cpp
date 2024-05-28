#include "sendFunction.h"
#include <cstring>  // Para memset

// Define fuction to send request to server
int sendRequest(chat::Request* request, int clientSocket) {
    if (request == nullptr || request->ByteSizeLong() > BUFFER_SIZE) {
        return -1;
    }

    std::string serializedRequest;
    if (!request->SerializeToString(&serializedRequest)) {
        std::cerr << "Failed to serialize request." << std::endl;
        return -1;
    }

    if (send(clientSocket, serializedRequest.c_str(), serializedRequest.size(), 0) < 0) {
        std::cerr << "Failed to send request." << std::endl;
        return -1;
    }
    return 0;
}

// Define function to send response to client
int sendResponse(chat::Response* response, int clientSocket) {
    if (response == nullptr || response->ByteSizeLong() > BUFFER_SIZE) {
        if(response == nullptr){
            std::cerr << "Response is null" << std::endl;
        }

        if(response->ByteSizeLong() > BUFFER_SIZE){
            std::cerr << "Response is too big" << std::endl;
        }
        return -1;
    }

    std::string serializedResponse;
    if (!response->SerializeToString(&serializedResponse)) {
        std::cerr << "Failed to serialize response." << std::endl;
        return -1;
    }

    if (send(clientSocket, serializedResponse.c_str(), serializedResponse.size(), 0) < 0) {
        std::cerr << "Failed to send response." << std::endl;
        return -1;
    }
    return 0;
}

// Define function to obtain request from client
int getRequest(chat::Request* request, int clientSocket) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer

    int receivedSize = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (receivedSize <= 0) {
        std::cerr << "No message received or failed to receive message." << std::endl;
        return (receivedSize == 0) ? 2 : -1;
    }

    std::string serializedRequest(buffer, receivedSize);
    if (serializedRequest.empty()) {
        std::cerr << "Empty message received." << std::endl;
        return -1;
    }

    if (!request->ParseFromString(serializedRequest)) {
        std::cerr << "Failed to parse message." << std::endl;
        return -1;
    }

    std::cout << "Message received from " << clientSocket << std::endl;
    return 0;
}

// Define function to obtain response from server
int getResponse(chat::Response* response, int clientSocket) {
    response->Clear();
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer

    int receivedSize = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (receivedSize <= 0) {
        std::cerr << "Failed to receive response." << std::endl;
        return -1;
    }

    std::string serializedResponse(buffer, receivedSize);
    if (serializedResponse.empty()) {
        std::cerr << "Empty response received." << std::endl;
        return -1;
    }

    if (!response->ParseFromString(serializedResponse)) {
        std::cerr << "Failed to parse response." << std::endl;
        return -1;
    }
    return 0;
}
