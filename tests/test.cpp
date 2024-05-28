#include "../proto/chat.pb.h"
#include "../src/sendFunction.h"
#include <pthread.h>
#include <string>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include <stdlib.h>
#include <netinet/in.h>

struct Test_data {
    int clientSocket;
    std::string message;
    std::string username;
};

void *test1as(void *arg)
{
    Test_data *data = static_cast<Test_data *>(arg);
    int clientSocket = data->clientSocket;
    std::string message = data->message;
    std::string username = data->username;
    chat::Request request;
    std::cout << "Test1"<<message << std::endl;

    request.set_operation(chat::REGISTER_USER);
    chat::NewUserRequest *newUserRequest = request.mutable_register_user();
    newUserRequest->set_username(username);

    // Send request to server
    sendRequest(&request, clientSocket);
    chat::Response response_register;
    getResponse(&response_register, clientSocket);

    while (true)
    {
        chat::Request request_chat;
        request_chat.set_operation(chat::SEND_MESSAGE);
        chat::SendMessageRequest *sendMessageRequest = request_chat.mutable_send_message();
        sendMessageRequest->set_content(message);

        sendRequest(&request_chat, clientSocket);
        std::cout << "Message sent" << std::endl;
        // Sleep for 50 second
        sleep(1);
    }
}

int main()
{
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(4000);
    serverAddr.sin_addr.s_addr = inet_addr("3.141.42.86");

    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Connection failed" << std::endl;
        return 1;
    }

    // Create another connection to server
    int clientSocket2 = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr2;
    serverAddr2.sin_family = AF_INET;
    serverAddr2.sin_port = htons(4000);
    serverAddr2.sin_addr.s_addr = inet_addr("3.141.42.86");

    if (connect(clientSocket2, (struct sockaddr *)&serverAddr2, sizeof(serverAddr2)) < 0)
    {
        std::cerr << "Connection failed" << std::endl;
        return 1;
    }

    std::string option;
    std::cout << "Enter option: ";
    std::cin >> option;

    pthread_t test1;
    pthread_t test2;

    Test_data* data1 = new Test_data;  // Asignar memoria para data1
    data1->clientSocket = clientSocket;
    data1->message = "Hello from test1";
    data1->username = "test1";


    Test_data* data2 = new Test_data;  // Asignar memoria para data2
    data2->clientSocket = clientSocket2;
    data2->message = "Hello from test2";
    data2->username = "test2";

    if (pthread_create(&test1, nullptr, test1as, (void*)data1) != 0) {
        std::cerr << "Error al crear el hilo del temporizador." << std::endl;
    }
    if (pthread_create(&test2, nullptr, test1as, (void*)data2) != 0) {
        std::cerr << "Error al crear el hilo del temporizador." << std::endl;
    }

    pthread_join(test1, nullptr);
    pthread_join(test2, nullptr);

    // Liberar memoria asignada
    delete data1;
    delete data2;

    return 0;
}
