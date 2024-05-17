#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include "./proto/chat.pb.h"
#include "constants.h"
#include "./src/sendFunction.h"
#include "nlohmann/json.hpp"
#include <mutex>
#include <queue>
#include <condition_variable>

struct ClientInfo {
    int socket;
    std::string ipAddress;
    std::queue<chat::Response>* responses;
    std::mutex responsesMutex;
    bool connected;
    std::condition_variable condition;
};

using json = nlohmann::json;

json clients;
std::mutex clientsMutex;
std::queue<chat::Response> messages;
std::mutex messagesMutex;
std::condition_variable messagesCondition;
std::vector<ClientInfo*> clientsInfo;
std::mutex clientsInfoMutex;
json onlineUsers;

void* handleThreadMessages(void* arg) {
    while (true) {
        std::unique_lock<std::mutex> lock(messagesMutex);
        messagesCondition.wait(lock, [] { return !messages.empty(); });

        chat::Response message = messages.front();
        messages.pop();
        lock.unlock();

        std::lock_guard<std::mutex> clientsLock(clientsInfoMutex);
        for (ClientInfo* info : clientsInfo) {
            std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
            info->responses->push(message);
            info->condition.notify_all();
        }
    }
    return nullptr;
}

void sendMessage(chat::Request* request, ClientInfo* info, const std::string& sender) {
    chat::Response response_message;
    response_message.set_operation(chat::INCOMING_MESSAGE);
    response_message.set_status_code(chat::OK);
    response_message.set_message("Mensaje enviado exitosamente.");
    response_message.mutable_incoming_message()->set_sender(sender);
    response_message.mutable_incoming_message()->set_content(request->send_message().content());
    
    {
        std::lock_guard<std::mutex> lock(messagesMutex);
        messages.push(response_message);
    }
    
    messagesCondition.notify_one();
}

void sendUsersList(ClientInfo* info) {
    chat::Response response;
    response.set_operation(chat::GET_USERS);
    response.set_status_code(chat::OK);
    response.set_message("Lista de usuarios.");
    chat::UserListResponse* userList = response.mutable_user_list();
    userList->set_type(chat::ALL);
    
    for (const auto& user : onlineUsers) {
        chat::User* usr = userList->add_users();
        usr->set_username(user);
    }

    {
        std::lock_guard<std::mutex> lock(info->responsesMutex);
        info->responses->push(response);
    }
    info->condition.notify_all();
}

void* handleListenClient(void* arg) {
    ClientInfo* info = static_cast<ClientInfo*>(arg);
    int clientSocket = info->socket;

    chat::Request first_request;
    int status = getRequest(&first_request, clientSocket);
    if (status == -1 || status == 2) {
        std::cerr << "Error al recibir la solicitud o cliente desconectado." << std::endl;
        return nullptr;
    }

    if (first_request.operation() != chat::REGISTER_USER) {
        std::cerr << "Operación inválida." << std::endl;
        return nullptr;
    }

    std::string userName = first_request.register_user().username();
    json client;
    client["ip"] = info->ipAddress;
    client["socket"] = clientSocket;
    client["status"] = "online";

    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        if (clients.find(userName) != clients.end()) {
            if (clients[userName]["ip"] != info->ipAddress) {
                chat::Response badResponse;
                badResponse.set_operation(chat::REGISTER_USER);
                badResponse.set_status_code(chat::BAD_REQUEST);
                badResponse.set_message("User already registered.");
                std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
                info->responses->push(badResponse);
                info->condition.notify_all();
                return nullptr;
            } else {
                chat::Response goodResponse;
                goodResponse.set_operation(chat::REGISTER_USER);
                goodResponse.set_status_code(chat::OK);
                goodResponse.set_message("Successfully logged in.");
                std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
                info->responses->push(goodResponse);
                info->condition.notify_all();
            }
        } else {
            clients[userName] = client;
            chat::Response goodResponse;
            goodResponse.set_operation(chat::REGISTER_USER);
            goodResponse.set_status_code(chat::OK);
            goodResponse.set_message("Successfully registered.");
            std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
            info->responses->push(goodResponse);
            info->condition.notify_all();
        }
    }

    onlineUsers.push_back(userName);

    while (info->connected) {
        chat::Request request;
        int status = getRequest(&request, clientSocket);
        if (status == -1 || status == 2) {
            std::cerr << userName << " se desconectó." << std::endl;
            break;
        }

        switch (request.operation()) {
            case chat::SEND_MESSAGE:
                sendMessage(&request, info, userName);
                break;
            case chat::GET_USERS:
                sendUsersList(info);
                break;
            default:
                break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients.erase(userName);
    }

    auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
    if (it != onlineUsers.end()) {
        onlineUsers.erase(it);
    }

    close(clientSocket);
    return nullptr;
}

void* handleResponseClient(void* arg) {
    ClientInfo* info = static_cast<ClientInfo*>(arg);
    int clientSocket = info->socket;

    while (info->connected || !info->responses->empty()) {
        std::unique_lock<std::mutex> lock(info->responsesMutex);
        info->condition.wait(lock, [info] { return !info->responses->empty() || !info->connected; });

        if (!info->responses->empty()) {
            chat::Response response = info->responses->front();
            info->responses->pop();
            lock.unlock();

            int status = sendResponse(&response, clientSocket);
            if (status == -1) {
                std::cerr << "Error al enviar la respuesta." << std::endl;
                break;
            }
            std::cout << "Respuesta enviada." << std::endl;
        } else {
            lock.unlock();
        }
    }

    close(clientSocket);
    return nullptr;
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Error al crear el socket del servidor." << std::endl;
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(4000);

    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        std::cerr << "Error al vincular el socket." << std::endl;
        close(serverSocket);
        return 1;
    }

    if (listen(serverSocket, 5) == -1) {
        std::cerr << "Error al escuchar conexiones entrantes." << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "Servidor TCP iniciado. Esperando conexiones..." << std::endl;

    pthread_t messageThread;
    if (pthread_create(&messageThread, nullptr, handleThreadMessages, nullptr) != 0) {
        std::cerr << "Error al crear el hilo para manejar mensajes." << std::endl;
        close(serverSocket);
        return 1;
    }

    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientAddressLength = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
        if (clientSocket == -1) {
            std::cerr << "Error al aceptar la conexión entrante." << std::endl;
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddress.sin_addr), clientIP, INET_ADDRSTRLEN);

        ClientInfo* clientInfo = new ClientInfo();
        clientInfo->socket = clientSocket;
        clientInfo->ipAddress = clientIP;
        clientInfo->responses = new std::queue<chat::Response>();
        clientInfo->connected = true;

        std::cout << "Cliente conectado desde " << clientInfo->ipAddress << ":" << clientInfo->socket << "." << std::endl;

        pthread_t clientThread, responseThread;
        if (pthread_create(&clientThread, nullptr, handleListenClient, (void*)clientInfo) != 0) {
            std::cerr << "Error al crear el hilo para el cliente." << std::endl;
            close(clientSocket);
            delete clientInfo;
            continue;
        }

        if (pthread_create(&responseThread, nullptr, handleResponseClient, (void*)clientInfo) != 0) {
            std::cerr << "Error al crear el hilo para las respuestas del cliente." << std::endl;
            close(clientSocket);
            delete clientInfo;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clientsInfoMutex);
            clientsInfo.push_back(clientInfo);
        }
    }

    close(serverSocket);
    return 0;
}
