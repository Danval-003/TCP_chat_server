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
#include <stdlib.h>
using namespace std;

// Set the maximum buffer (message) size to 5000 bytes.
// This limit is predetermined to ensure sufficient space for data processing,
// preventing buffer overflow and maintaining system stability.
#define BUFFER_SIZE 5000
char username[100]; // Current client username.

// Boolean to handle waiting status
std::atomic<bool>awaitingResponse{false};

// Queue to store the incoming messages from the server (FIFO),
// to be used only on broadcasting and private msg. on the
// general chat.
std::queue<chat::IncomingMessageResponse> messagesQueue;

// Stack that contains possible user status.
// ONLY USED WHEN PRINTING.
std::vector<std::string> user_status = {"Online", "Busy", "Offline"};

volatile int threadExit = 1;

struct ThreadParams {
    int clientSocket;
    pthread_t* receptor_pthread;
};

// Function to print either the main menu or the help menu based on the parameter
void printMenu(bool isMainMenu) {
    // List of menu items for the main menu
    std::vector<std::string> mainMenuItems = {
        "1. Show Messages",
        "2. Broadcasting",
        "3. Send Direct Message",
        "4. Change Status",
        "5. List Active Users",
        "6. Get User Information",
        "7. Help",
        "8. Exit"
    };

    // List of help items describing each menu option
    std::vector<std::string> helpMenuItems = {
        "1. Show Messages: Displays all your messages.",
        "2. Broadcasting: Send a message to all users.",
        "3. Send Direct Message: Send a private message to a user.",
        "4. Change Status: Update your current status.",
        "5. List Active Users: See a list of users currently online.",
        "6. Get User Information: Retrieve information about a user.",
        "7. Help: Displays this help menu.",
        "8. Exit: Exit the chat application."
    };

    // Title for the menu based on the parameter
    const std::string title = isMainMenu ? "OS Chat Main Menu" : "OS Chat Help Menu";
    // Width of the menu
    const int width = 75;
    // Character used for the border
    const char borderChar = '.';

    // Lambda function to print a border line with a given character and length
    auto printBorder = [&](char ch, int count) {
        std::cout << std::string(count, ch) << std::endl;
    };

    // Lambda function to print centered text within a border
    auto printCentered = [&](const std::string& text, int width, char borderChar) {
        // Calculate padding needed to center the text
        int padding = (width - text.length()) / 2;
        int paddingLeft = padding;
        int paddingRight = padding;

        // If the padding is not even, adjust the right padding
        if ((width - text.length()) % 2 != 0) {
            paddingRight++;
        }

        // Print the text centered with the border character
        std::cout << borderChar << std::string(paddingLeft, ' ') << text 
                  << std::string(paddingRight, ' ') << borderChar << std::endl;
    };

    // Print top border
    printBorder(borderChar, width);

    // Print title centered
    printCentered(title, width - 2, borderChar);

    // Print separating border
    printBorder(borderChar, width);

    // Print each menu item centered based on the parameter
    const auto& items = isMainMenu ? mainMenuItems : helpMenuItems;
    for (const auto& item : items) {
        printCentered(item, width - 2, borderChar);
    }

    // Print bottom border
    printBorder(borderChar, width);
}

// Function to register a new user on the server
void registerUser(int clientSocket, std::string username) {
    // Create a new request object for user registration
    chat::Request request;
    request.set_operation(chat::REGISTER_USER); // Specify the operation type as user registration

    // Set up the new user's details in the request
    auto *newUser = request.mutable_register_user(); // Get a pointer to the user registration part of the request
    newUser->set_username(username); // Set the username for the new user

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

    std::cout << response.message() << std::endl; // Print the success message from the server

    // Handle the server's response
    if (response.status_code() == chat::BAD_REQUEST) { // Check if the registration failed due to a bad request
        exit(1); // Exit the program with an error code
    }

}

void getActiveUsers(int clientSocket) {
    // Create a request object
    chat::Request request;

    // Set the operation type to GET_USERS in the request
    request.set_operation(chat::GET_USERS); 
    auto *user_details = request.mutable_get_users();
    user_details -> set_username("");

    // Send the request to the server using the specified client socket
    if (sendRequest(&request, clientSocket) < 0) {
        // If sending the request fails, print an error message and exit the program
        std::cout << "\nFailed to send request." << std::endl; 
        exit(1); 
    }
}

void getSingleUser(int clientSocket) {
    std::string username;

    if (username.empty()) {    
        // Prompt the user to enter the username they are searching for
        std::cout << "Please enter the username you'd like to search for: " << std::endl;
        // Read user input
        std::getline(std::cin, username);
    }

    // Create a request object to send to the server
    chat::Request request;

    // Set the operation type to GET_USERS in the request object
    request.set_operation(chat::GET_USERS); 

    // Set the username in the get_users field of the request
    auto *user_details = request.mutable_get_users();
    user_details -> set_username(username);

    // Send the request to the server using the specified client socket
    if (sendRequest(&request, clientSocket) < 0) {
        // If sending the request fails, print an error message and exit the program
        std::cout << "\nFailed to send request." << std::endl; 
        exit(1); 
    }

    // Clear the username string after sending the request
    username.clear();
}

void handleBroadcasting(int clientSocket, int privateMessage = 0) {
    std::string msg;
    std::string recipient;

    if (privateMessage) {
        // Prompt the user to enter the message they want to broadcast
        std::cout << "Enter the recipient's username: " << std::endl;

        // Read the entire line of input from the user
        std::getline(std::cin, recipient);
    }

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

    if (privateMessage) {
        requestmsg->set_recipient(recipient);
    }

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
    request_status -> set_username(username);

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

void* messageDequeue(void*) {

    while(threadExit) {
        
        if (!messagesQueue.empty()) {
            const auto& msg = messagesQueue.front();
            if (msg.type() == chat::MessageType::BROADCAST) {
                std::cout << BOLD << CYAN << msg.sender() << ": " << RESET << msg.content() << std::endl;
            } else {
                std::cout << RED <<"(Private Message) " << BOLD << CYAN << msg.sender() << ": " << RESET << msg.content() << std::endl;
            }
            messagesQueue.pop();
        }

    }

    pthread_exit(NULL);

}

void showMessages() {
    std::string choice_str;
    int choice;

    // Display available status options
    std::cout << "\nEnter '0' to exit at any time.\n";

    threadExit = 1;

    pthread_t pthread_message_dequeue;
    pthread_create(&pthread_message_dequeue, NULL, messageDequeue, NULL);

    while(1) {
        if (choice_str.empty()) {    
            // Read user input
            std::getline(std::cin, choice_str);
        }

        // Check if the input is a valid digit
        if (std::all_of(choice_str.begin(), choice_str.end(), ::isdigit)) {
            // Convert string to integer
            choice = std::stoi(choice_str);
            // Adjust for 0-based indexing
        } else {
            // Notify the user of invalid input
            std::cout << "Invalid input. Please enter a number.\n";
        }

        if (choice == 0) {
            threadExit = 0;
            break;
        }
    }
    
    pthread_join(pthread_message_dequeue, NULL);
    
}

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
            std::cout << response.message() << std::endl;
            break;

        case 2:
            std::cout << response.message() << std::endl;
            break;

        case 3:
            if (!response.has_user_list()) {
                // If no user list is found in the response, print an informative message
                std::cout << "No Active Users Found." << std::endl; 
            } else {
                const auto &user_list = response.user_list();
                
                if (user_list.type() == chat::UserListType::ALL) {
                    std::cout << "Currently Active Users:" << std::endl;
                    // Iterate through the users in the user list and print each username
                    for (const auto& user : user_list.users()) {
                        std::cout << "Username: " << user.username() << " -> " << user_status[user.status()] << std::endl;
                    }
                } else if (user_list.type() == chat::UserListType::SINGLE) {
                    std::cout << "User Information:" << std::endl;
                    // Iterate through the users in the list and print their usernames
                    for (const auto& user : user_list.users()) {
                        std::cout << "Username: " << user.username() << " -> " << user_status[user.status()] << std::endl;
                    }
                }
            }
            break;

        case 4:
            std::cout << response.message() << std::endl;
            awaitingResponse = false;
            pthread_exit(NULL);
            break;

        case 5:
            messagesQueue.push(response.incoming_message());
            break;

        }
        
        awaitingResponse = false;
    }

    // Return nullptr to indicate the thread's completion
    return nullptr;
};

void unregister(int clientSocket) {
    // Create a request object
    chat::Request request;

    // Set the operation type to update status in the request
    request.set_operation(chat::Operation::UNREGISTER_USER);

    // Get a pointer to the mutable unregister_user field in the request
    auto *request_user = request.mutable_unregister_user();

    request_user -> set_username(username);

    sendRequest(&request, clientSocket); 
}

// Main functions as a the thread to handle server requests.
int main(int argc, char* argv[]) {
    int choice;
    std::string choiceStr;
    bool isRunnig = true;

    // Check if the number of command-line arguments is exactly 4
    if (argc != 4) {
        printf("Usage: %s <username> <serverip> <port>\n", argv[0]);
        return 1; // Exit the program with an error code
    }


    char serverip[100]; // Current client serverip.
    int port; // Current client port.

    // Assign command-line arguments to the respective variables
    strcpy(username, argv[1]);
    strcpy(serverip, argv[2]);
    port = atoi(argv[3]);
    

    // Print the values to verify
    std::cout << username << "\n";
    std::cout << serverip << "\n";
    std::cout << port << "\n";

    // Create a socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
    }

    // Set up the server address and port
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);  // Replace with the actual server port
    if (inet_pton(AF_INET, serverip, &(serverAddress.sin_addr)) <= 0) {
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
    registerUser(clientSocket, username);

    // Create pthread to handle server responses.
    pthread_t pthread_response;
    pthread_create(&pthread_response, NULL, listener, (void*)&clientSocket);
    ThreadParams tp = {clientSocket, &pthread_response};

    while (isRunnig) {

        // Wait until awaitingResponse is false
        while (awaitingResponse) {
            // Sleep for a short time to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (choiceStr.empty()) {
            printMenu(true);
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
                showMessages();
                break;

            case 2:
                handleBroadcasting(clientSocket);
                break;

            case 3:
                handleBroadcasting(clientSocket, 1);
                break;

            case 4:
                awaitingResponse = true;
                handleStatusChange(clientSocket);
                break;

            case 5:
                awaitingResponse = true;
                getActiveUsers(clientSocket);
                break;

            case 6:
                awaitingResponse = true;
                getSingleUser(clientSocket);
                break;

            case 7:
                printMenu(false);
                break;

            case 8:
                isRunnig = false;
                std::cout << "Exiting now..." << std::endl;
                unregister(clientSocket);
                awaitingResponse = true;
                while (awaitingResponse);
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
