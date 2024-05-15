#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "chat.pb.h"
#include <pthread.h>
#include <string>

struct ThreadParams {
    int clientSocket;
    pthread_t* receptor_pthread;
};

void* receptorFunction(void* arg) {
    int clientSocket = *((int*)arg);

    while (true) {
        // Receive response size
        uint32_t responseSize;
        if (recv(clientSocket, &responseSize, sizeof(responseSize), 0) <= 0) {
            std::cerr << "Failed to receive response size." << std::endl;
            break;
        }
        chat::Response response;

        // Receive response
        char buffer[responseSize];
        if (recv(clientSocket, buffer, responseSize, 0) <= 0) {
            std::cerr << "Failed to receive response." << std::endl;
            break;
        }

        // Parse response
        if (!response.ParseFromArray(buffer, responseSize)) {
            std::cerr << "Failed to parse response." << std::endl;
            break;
        }

        std::cout << "Server: " << response.message() << std::endl;

    }
    return nullptr; // Agregar declaración de retorno
}

void* senderFunction(void* arg) {
    ThreadParams tp = *((ThreadParams*)arg);
    int clientSocket = tp.clientSocket;
    bool isRunnig = true;
    while (isRunnig) {
        // Create request
        chat::Request request;
        std::cout << "What do you want to do?" << std::endl;
        std::string option;
        std::getline(std::cin, option);

        int legthOption = option.length();

        if (legthOption<=1){

            char optionChar = option[0];

            switch (optionChar) {
                case '1': {
                    // Send message
                    request.set_operation(chat::SEND_MESSAGE);
                    std::cout << "Enter the message: ";
                    std::string message;
                    std::getline(std::cin, message);
                    std::cout<<message<<std::endl;
                    request.mutable_send_message()->set_content(message);

                    // Serialize request
                    uint32_t requestSize = request.ByteSizeLong(); // Use ByteSizeLong()
                    char buffer[requestSize];
                    if (!request.SerializeToArray(buffer, requestSize)) {
                        std::cerr << "Failed to serialize request." << std::endl;
                        break;
                    }

                    std::cout<<requestSize<<std::endl;
                    // Send request size
                    if (send(clientSocket, &requestSize, sizeof(requestSize), 0) < 0) {
                        std::cerr << "Failed to send request size." << std::endl;
                        break;
                    }

                    // Send request
                    if (send(clientSocket, buffer, requestSize, 0) < 0) {
                        std::cerr << "Failed to send request." << std::endl;
                        break;
                    }

                    break;
                }
                default:
                    isRunnig = false;
                    
                    std::cout<<"Saliendo del chat..."<<std::endl;
                    break;
            }
        } else {
            std::cout<<"Saliendo del chat..."<<std::endl;
            isRunnig = false;
        }

    }

    pthread_cancel(*tp.receptor_pthread);
    return nullptr; // Agregar declaración de retorno
}

int main() {
    // Create a socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
    }

    // Set up the server address and port
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(4000);  // Replace with the actual server port
    if (inet_pton(AF_INET, "18.191.89.30", &(serverAddress.sin_addr)) <= 0) {
        std::cerr << "Invalid address/Address not supported." << std::endl;
        return 1;
    }

    // Connect to the server
    if (connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Connection failed." << std::endl;
        return 1;
    }

    // Create pthread to receive messages
    pthread_t receptorThread;
    pthread_create(&receptorThread, NULL, receptorFunction, (void*)&clientSocket);
    ThreadParams tp = {clientSocket, &receptorThread};

    // Create pthread to send messages
    pthread_t senderThread;
    pthread_create(&senderThread, NULL, senderFunction, (void*)&tp);


    // Wait for the threads to finish
    pthread_join(receptorThread, NULL);
    pthread_join(senderThread, NULL);

    // Close the socket
    close(clientSocket);

    return 0;
}
