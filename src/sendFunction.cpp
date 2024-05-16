#include "sendFunction.h"


// Define fuction to send request to server
int sendRequest(chat::Request* request, int clientSocket) {
    // Verify if the request is not null or exceeds the maximum buffer size
    if (request == nullptr || request->ByteSizeLong() > BUFFER_SIZE) {
        return -1;
    }

    // Create a buffer to store the serialized request
    char buffer[BUFFER_SIZE];
    // Serialize the request into a string
    std::string serializedRequest;
    request->SerializeToString(&serializedRequest);
    // Copy the serialized request into the buffer
    strcpy(buffer, serializedRequest.c_str());
    // Send the serialized request to the server and verify is this operation was successful
    if (send(clientSocket, buffer, serializedRequest.size(), 0) < 0) {
        return -1;
    }
    return 0;
}

// Define function to send response to client
int sendResponse(chat::Response* response, int clientSocket) {
    // Verify if the response is not null or exceeds the maximum buffer size
    if (response == nullptr || response->ByteSizeLong() > BUFFER_SIZE) {
        return -1;
    }

    // Create a buffer to store the serialized response
    char buffer[BUFFER_SIZE];
    // Serialize the response into a string and verify is this operation was successful
    std::string serializedResponse;
    if (!response->SerializeToString(&serializedResponse)) {
        return -1;
    }
    // Copy the serialized response into the buffer
    strcpy(buffer, serializedResponse.c_str());
    // Send the serialized response to the client and verify is this operation was successful
    if (send(clientSocket, buffer, serializedResponse.size(), 0) < 0) {
        return -1;
    }
    return 0;
}


// Define fuction to obtain request from client
int getRequest(chat::Request* request, int clientSocket) {
    // Create a buffer to store the serialized request
    char buffer[BUFFER_SIZE];
    // Receive the serialized request from the client and verify is this operation was successful and if dont recive any message return 2
    if (recv(clientSocket, buffer, BUFFER_SIZE, 0) <= 0) {
        return 2;
    }

    // Parse the serialized request into a string and verify is this operation was successful
    std::string serializedRequest(buffer);
    if (serializedRequest.empty()) {
        return -1;
    }
    // Parse the serialized request into a request object and verify is this operation was successful
    if (!request->ParseFromString(serializedRequest)) {
        return -1;
    }
    return 0;
}

// Define function to obtain response from server
int getResponse(chat::Response* response, int clientSocket) {
    // Create a buffer to store the serialized response
    char buffer[BUFFER_SIZE];
    // Receive the serialized response from the server and verify is this operation was successful
    if (recv(clientSocket, buffer, BUFFER_SIZE, 0) < 0) {
        return -1;
    }

    // Parse the serialized response into a string and verify is this operation was successful
    std::string serializedResponse(buffer);
    if (serializedResponse.empty()) {
        return -1;
    }
    // Parse the serialized response into a response object and verify is this operation was successful
    if (!response->ParseFromString(serializedResponse)) {
        return -1;
    }
    return 0;
}
