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
#include "./src/sendFunction.h"
#include <vector>
using namespace std;
 

// Set the maximum buffer (message) size to 5000 bytes.
// This limit is predetermined to ensure sufficient space for data processing,
// preventing buffer overflow and maintaining system stability.
#define BUFFER_SIZE 5000

// Boolean to handle waiting status
bool awaitingResponse = false;

// Queue to store the incoming messages from the server (FIFO)
std::queue<std::string> messages;

struct ThreadParams {
    int clientSocket;
    pthread_t* receptor_pthread;
};

void printMenu() {
    std::vector<std::string> menuItems = {
        "1. Broadcasting",
        "2. Send Direct Message",
        "3. Change Status",
        "4. List Active Users",
        "5. Get User Information",
        "6. Help",
        "7. Exit"
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

// Function to register a new user on the server
void registerUser(int clientSocket) {
    // Create a new request object for user registration
    chat::Request request;
    request.set_operation(chat::REGISTER_USER); // Specify the operation type as user registration

    // Set up the new user's details in the request
    auto *newUser = request.mutable_register_user(); // Get a pointer to the user registration part of the request
    newUser->set_username(TEST_USERNAME); // Set the username for the new user

    // Send the registration request to the server
    // If the request fails, print an error message and exit the program
    if (sendRequest(&request, clientSocket) < 0) { // Check if the request sending was successful
        std::cout << "\nFailed to send request." << std::endl; // Print an error message if sending failed
        exit(1); // Exit the program with an error code
    }

    // Create a response object to hold the server's response
    chat::Response response;
    if (getResponse(&response, clientSocket) < 0) { // Receive the response from the server
        std::cout << "\nFailed to receive response." << std::endl; // Print an error message if receiving failed
        exit(1); // Exit the program with an error code
    }

    // Handle the server's response
    if (response.status_code() == chat::BAD_REQUEST) { // Check if the registration failed due to a bad request
        // Inform the user that the username is already taken
        std::cout << "\nRegistration failed: The username '" << TEST_USERNAME 
                  << "' is already taken. Please choose a different username." << std::endl;
        exit(1); // Exit the program with an error code
    }

    // Check if the registration was successful
    if (response.status_code() == chat::OK) { // If the response status is OK
        std::cout << response.message() << std::endl; // Print the success message from the server
    }
}

void getActiveUsers(int clientSocket) {

    chat::Request request;
    request.set_operation(chat::GET_USERS); 

    if (sendRequest(&request, clientSocket) < 0) { 
        std::cout << "\nFailed to send request." << std::endl; 
        exit(1); 
    }

}

// Function to continuously listen responses from server
void* listener(void* arg) {
    int clientSocket = *((int*)arg);

    // Continuously listen for messages from the server
    while (true) {
        chat::Response response;

        if (getResponse(&response, clientSocket) < 0) {
            std::cout << "\nFailed to receive response." << std::endl; 
            exit(1);
        }

        switch (response.operation()){

        case 1:
            // TODO: Code to send direct message.
            break;

        case 2:
            // TODO: Code to change user status.
            break;

        case 3:
            if (!response.has_user_list()) {
                std::cout << "\nNo active users found." << std::endl; 
            } else {
                const auto &user_list = response.user_list();
                std::cout << "Active users:" << std::endl;
                for (const auto& user : user_list.users()) {
                    std::cout << user.username() << std::endl;
                }
            }
            break;

        case 4:
            // TODO: Code to get user information.
            break;

        case 5:
            // TODO: Code to print help menu.
            break;
        }
    }

    // Return nullptr to indicate the thread's completion
    return nullptr;
};

// Main functions as a the thread to handle server requests.
int main(int argc, char* argv[]) {
    std::string choice;
    bool isRunnig = true;

    // Check if the number of command-line arguments is exactly 4
    if (argc != 3) {
        printf("Usage: %s <username> <serverip> <port>\n", argv[0]);
        //return 1; // Exit the program with an error code
    }

    //char username[100]; // Current client username.
    //char serverip[100]; // Current client serverip.
    //char port[100]; // Current client port.

    // Assign command-line arguments to the respective variables
    //sprintf(username, "%s", argv[1]);
    //sprintf(serverip, "%s", argv[2]);
    //sprintf(port, "%s", argv[3]);

    // Print the values to verify
    std::cout << TEST_USERNAME << "\n";
    std::cout << SERVER_IP << "\n";
    std::cout << PORT << "\n";

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

    // The following line calls the function to register a new user.
    // it will send a registration request to the server using the provided client socket.
    registerUser(clientSocket);

    // Create pthread to handle server responses.
    pthread_t pthread_response;
    pthread_create(&pthread_response, NULL, listener, (void*)&clientSocket);
    ThreadParams tp = {clientSocket, &pthread_response};

    printMenu();

    while(isRunnig){
        std::cout << "What would you like to do? " << std::endl;

        getline(std::cin,choice);
        int choice_int = stoi(choice);
        
        switch (choice_int){
        case 1:
            // TODO: Code to send general message.
            break;

        case 2:
            // TODO: Code to send direct message.
            break;

        case 3:
            // TODO: Code to change user status.
            break;

        case 4:
            getActiveUsers(clientSocket);
            break;

        case 5:
            // TODO: Code to get user information.
            break;

        case 6:
            // TODO: Code to print help menu.
            break;
        case 7:
            isRunnig = false;

        default:
            std::cout << "Not a valid choice! \n";
            break;
        }
    };

    // Wait for the threads to finish
    pthread_join(pthread_response, NULL);

    // Close the socket
    close(clientSocket);

    return 0;
}
