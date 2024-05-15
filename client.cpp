#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "chat.pb.h"
#include <pthread.h>
#include <string>
#include <iomanip>
#include "constants.h"

struct ThreadParams {
    int clientSocket;
    pthread_t* receptor_pthread;
};

void* receptorFunction(void* arg) {
    int clientSocket = *((int*)arg);

    while (true) {
        chat::Response response;

        // Receive response
        char buffer[BUFFER_SIZE];
        if (recv(clientSocket, buffer, BUFFER_SIZE, 0) <= 0) {
            std::cerr << "Failed to receive response." << std::endl;
            break;
        }

        // Parse response
        if (!response.ParseFromArray(buffer, BUFFER_SIZE)) {
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

                    char buffer[BUFFER_SIZE];

                    
                    if (!request.SerializeToArray(buffer, BUFFER_SIZE)) {
                        std::cerr << "Failed to serialize request." << std::endl;
                        break;
                    }

                    // Print serialized data (for debugging purposes)
                    std::cout << "Serialized data: " << std::endl;
                    for (int i = 0; i < request.ByteSizeLong(); ++i) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]) << " ";
                    }
                    std::cout << std::endl;

                    // Send request
                    if (send(clientSocket, buffer, BUFFER_SIZE, 0) < 0) {
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
    serverAddress.sin_port = htons(PORT);  // Replace with the actual server port
    if (inet_pton(AF_INET, SERVER_IP, &(serverAddress.sin_addr)) <= 0) {
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
