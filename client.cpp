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
#include <thread>
#include <vector>
using namespace std;

// Set the maximum buffer (message) size to 5000 bytes.
// This limit is predetermined to ensure sufficient space for data processing,
// preventing buffer overflow and maintaining system stability.
#define BUFFER_SIZE 5000

// Boolean to handle waiting status
std::atomic<bool>awaitingResponse{false};

// Queue to store the incoming messages from the server (FIFO),
// to be used only on broadcasting and private msg. on the
// general chat.
std::queue<std::string> messages;

// Stack that contains possible user status.
// ONLY USED WHEN PRINTING.
std::vector<std::string> user_status = {"Online", "Busy", "Offline"};

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
    // Create a request object
    chat::Request request;

    // Set the operation type to GET_USERS in the request
    request.set_operation(chat::GET_USERS); 

    // Send the request to the server using the specified client socket
    if (sendRequest(&request, clientSocket) < 0) {
        // If sending the request fails, print an error message and exit the program
        std::cout << "\nFailed to send request." << std::endl; 
        exit(1); 
    }
}

void handleBroadcasting(int clientSocket) {
    std::string msg;

    // Prompt the user to enter the message they want to broadcast
    std::cout << "Enter the message you'd like to broadcast: " << std::endl;

    // Read the entire line of input from the user
    std::getline(std::cin, msg);

    // Create a request object
    chat::Request request;

    // Set the operation type to SEND_MESSAGE in the request
    request.set_operation(chat::Operation::SEND_MESSAGE);

    // Get a pointer to the mutable send_message field in the request
    auto *requestmsg = request.mutable_send_message();

    // Set the content of the message in the request
    requestmsg->set_content(msg);

    // Send the request to the server using the specified client socket
    if (sendRequest(&request, clientSocket) < 0) {
        // If sending the request fails, print an error message and exit the program
        std::cout << "\nError: Unable to send the request." << std::endl;
        exit(1);
    }

    // Clear the message string after sending the request
    msg.clear();
};

// Function to handle status change for a client
void handleStatusChange(int clientSocket) {
    // Declare variables for user input
    std::string choice_str;
    int choice;

    // Display available status options
    std::cout << "Choose your status:\n";
    for (size_t i = 0; i < user_status.size(); ++i) {
        std::cout << (i + 1) << ". " << user_status[i] << std::endl;
    }
    
    if (choice_str.empty()) {    
        // Prompt the user to select a status
        std::cout << "\nEnter the number corresponding to your desired status:\n";
        // Read user input
        std::getline(std::cin, choice_str);
    }

    // Check if the input is a valid digit
    if (std::all_of(choice_str.begin(), choice_str.end(), ::isdigit)) {
        // Convert string to integer
        choice = std::stoi(choice_str);
        // Adjust for 0-based indexing
        choice -= 1;
    } else {
        // Notify the user of invalid input
        std::cout << "Invalid input. Please enter a number.\n";
    }

    // Create a request object
    chat::Request request;

    // Set the operation type to update status in the request
    request.set_operation(chat::Operation::UPDATE_STATUS);

    // Get a pointer to the mutable update_status field in the request
    auto *request_status = request.mutable_update_status();

    // Set the user's chosen status based on their input
    switch (choice){
    case 0:
        request_status -> set_new_status(chat::UserStatus::ONLINE);
        break;

    case 1:
        request_status -> set_new_status(chat::UserStatus::BUSY);
        break;

    case 2:
        request_status -> set_new_status(chat::UserStatus::OFFLINE);
        break;

    default:
        // Notify the user of an invalid status choice
        std::cout << "Invalid status choice.\n";
        return;
    }

    // Send the request to the server using the specified client socket
    if (sendRequest(&request, clientSocket) < 0) {
        // If sending the request fails, print an error message and exit the program
        std::cout << "\nError: Unable to send the request." << std::endl;
        exit(1);
    }

    // Clear variables
    choice_str.clear();
    choice = 0;
};

// Function to continuously listen responses from server
void* listener(void* arg) {
    int clientSocket = *((int*)arg);

    // Continuously listen for messages from the server
    while (true) {
        chat::Response response;

        if (getResponse(&response, clientSocket) < 0) {
            std::cout << "\nError: Unable to send the request." << std::endl;
            exit(1);
        }

        switch (response.operation()){
        case 1:
            // TODO: Code to send direct message.
            break;

        case 2:
            std::cout << response.message() << std::endl;
            break;

        case 3:
            // Check if the response contains a user list
            if (!response.has_user_list()) {
                // If no user list is found, print a message indicating no active users
                std::cout << "\nNo active users found." << std::endl; 
            } else {
                // If a user list is found, retrieve it
                const auto &user_list = response.user_list();
                std::cout << "Active users:" << std::endl;
                
                // Iterate through the users in the user list and print each username
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
        
        awaitingResponse = false;
    }

    // Return nullptr to indicate the thread's completion
    return nullptr;
};

// Main functions as a the thread to handle server requests.
int main(int argc, char* argv[]) {
    int choice;
    std::string choiceStr;
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

    while (isRunnig) {

        // Wait until awaitingResponse is false
        while (awaitingResponse) {
            // Sleep for a short time to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (choiceStr.empty()) {
            std::cout << "\nWhat would you like to do? " << std::endl;
            std::getline(std::cin, choiceStr);
            continue;
        }

        // Verify if the input is a digit
        if (std::all_of(choiceStr.begin(), choiceStr.end(), ::isdigit)) {
            choice = std::stoi(choiceStr);
        } else {
            std::cout << "Invalid input. Please enter a number.\n";
        }
        
        switch (choice) {
            case 1:
                handleBroadcasting(clientSocket);
                break;

            case 2:
                // TODO: Code to send direct message.
                break;

            case 3:
                awaitingResponse = true;
                handleStatusChange(clientSocket);
                break;

            case 4:
                awaitingResponse = true;
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
                std::cout << "Exiting now..." << std::endl;
                close(clientSocket);
                exit(0);
            default:
                std::cout << "Not a valid choice!" << std::endl;
                break;
        }

        choiceStr.clear();
        choice = 0;
    };

    // Wait for the threads to finish
    pthread_join(pthread_response, NULL);

    // Close the socket
    close(clientSocket);

    return 0;
}
