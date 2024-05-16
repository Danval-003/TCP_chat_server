#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "./proto/chat.pb.h"
#include <pthread.h>
#include <string>
#include <iomanip>
#include <queue>
#include <mutex>
#include "constants.h"

// Set the maximum buffer (message) size to 5000 bytes.
// This limit is predetermined to ensure sufficient space for data processing,
// preventing buffer overflow and maintaining system stability.
#define BUFFER_SIZE 5000

// Boolean to handle waiting status
bool awaitingResponse = false;

// Queue to store the incoming message from the server (FIFO)
std::queue<std::string> messages;

struct ThreadParams {
    int clientSocket;
    pthread_t* receptor_pthread;
};

void printMenu() {
    std::vector<std::string> menuItems = {
        "1. Send General Message",
        "2. Send Private Message",
        "3. List Active Users",
        "4. Exit"
    };

    const std::string title = "OS Chat Main Menu";
    const int width = 50;
    const char borderChar = '.';

    auto printBorder = [&](char ch, int count) {
        std::cout << std::string(count, ch) << std::endl;
    };

    auto printCentered = [&](const std::string& text, int width, char borderChar) {
        int padding = (width - text.length()) / 2;
        int paddingLeft = padding;
        int paddingRight = padding;

        if ((width - text.length()) % 2 != 0) {
            paddingRight++;
        }

        std::cout << borderChar << std::string(paddingLeft, ' ') << text 
                  << std::string(paddingRight, ' ') << borderChar << std::endl;
    };

    // Print top border
    printBorder(borderChar, width);

    // Print title
    printCentered(title, width - 2, borderChar);

    // Print separating border
    printBorder(borderChar, width);

    // Print menu items
    for (const auto& item : menuItems) {
        printCentered(item, width - 2, borderChar);
    }

    // Print bottom border
    printBorder(borderChar, width);
}

// Function to continuously listen for messages from the server
// and store them in a stack
void* listener(void* arg) {
    int clientSocket = *((int*)arg);

    // Continuously listen for messages from the server
    while (true) {
        chat::Response response;
    }

    // Return nullptr to indicate the thread's completion
    return nullptr;
}

void* senderFunction(void* arg) {

    ThreadParams tp = *((ThreadParams*)arg);
    int clientSocket = tp.clientSocket;
    bool isRunnig = true;

    while (isRunnig) {

        // Create request
        chat::Request request;
        printMenu();

        std::cout << "\nWhat do you want to do?" << std::endl;
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

                    // Send request
                    if (send(clientSocket, buffer, BUFFER_SIZE, 0) < 0) {
                        std::cerr << "Failed to send request." << std::endl;
                        break;
                    }
                    break;
                }
                default:
                    isRunnig = false;                    
                    std::cout<<"Exiting the chat now..."<<std::endl;
                    break;
            }
        } else {
            std::cout<<"Exiting the chat now..."<<std::endl;
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
    pthread_create(&receptorThread, NULL, listener, (void*)&clientSocket);
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
