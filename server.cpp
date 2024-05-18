
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
#include <ctime>

struct ClientInfo {
    int socket;
    std::string ipAddress;
    std::queue<chat::Response>* responses;
    std::mutex responsesMutex;
    bool connected;
    std::condition_variable condition;
    time_t lastMessage;
    std::mutex timerMutex;
    std::string userName;
    std::condition_variable itsNotOffline;
};

using json = nlohmann::json;

constexpr double TIMEOUT = 5.0;

json clients;
std::mutex clientsMutex;
std::queue<chat::Response> messages;
std::mutex messagesMutex;
std::condition_variable messagesCondition;
std::vector<ClientInfo*> clientsInfo;
std::mutex clientsInfoMutex;
std::mutex onlineUsersMutex;
json onlineUsers;

void* handleTimerClient(void* arg) {
    ClientInfo* info = static_cast<ClientInfo*>(arg);
    while (info->connected) {
        {
            std::unique_lock<std::mutex> lock(info->timerMutex);
            info->itsNotOffline.wait(lock, [info] { return info->connected && clients[info->userName]["status"] != chat::UserStatus::OFFLINE; });
        }
        double seconds = difftime(time(nullptr), info->lastMessage);
        if (seconds >= TIMEOUT) {
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients[info->userName]["status"] = chat::UserStatus::OFFLINE;
            }
            {
                std::lock_guard<std::mutex> lock(onlineUsersMutex);
                auto it = std::find(onlineUsers.begin(), onlineUsers.end(), info->userName);
                if (it != onlineUsers.end()) {
                    onlineUsers.erase(it);
                }
            }
            std::cout << info->userName << " se desconectó por inactividad. " << seconds << std::endl;
        }
    }
    return nullptr;
}

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
    std::string reciper = request->send_message().recipient();
    std::cout << "Mensaje de " << sender << " para " << reciper << ": " << request->send_message().content() << std::endl;
    if (reciper.empty()) {
        chat::Response response;
        response.set_operation(chat::SEND_MESSAGE);
        response.set_status_code(chat::OK);
        response.set_message("Message sent.");
        {
            std::lock_guard<std::mutex> lock(messagesMutex);
            messages.push(response);
        }
        messagesCondition.notify_all();
    } else {
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            if (clients.find(reciper) == clients.end()) {
                chat::Response response;
                response.set_operation(chat::SEND_MESSAGE);
                response.set_status_code(chat::BAD_REQUEST);
                response.set_message("User not found.");
                {
                    std::lock_guard<std::mutex> lock(info->responsesMutex);
                    info->responses->push(response);
                }
                info->condition.notify_all();
                return;
            }
        }

        chat::Response response;
        response.set_operation(chat::SEND_MESSAGE);
        response.set_status_code(chat::OK);
        response.set_message("Message sent.");
        chat::IncomingMessageResponse* msg = response.mutable_incoming_message();
        msg->set_content(request->send_message().content());
        msg->set_sender(sender);
        {
            std::lock_guard<std::mutex> lock(clientsInfoMutex);
            for (ClientInfo* clientInfo : clientsInfo) {
                if (clientInfo->userName == reciper) {
                    std::lock_guard<std::mutex> lock(clientInfo->responsesMutex);
                    clientInfo->responses->push(response);
                    clientInfo->condition.notify_all();
                    break;
                }
            }
        }
    }
}

void sendUsersList(ClientInfo* info) {
    chat::Response response;
    response.set_operation(chat::GET_USERS);
    response.set_status_code(chat::OK);
    response.set_message("Lista de usuarios.");
    chat::UserListResponse* userList = response.mutable_user_list();
    userList->set_type(chat::ALL);
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            std::cout << "Usuario: " << it.key() << std::endl;
        }
    }
    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        for (const std::string& userName : onlineUsers) {
            chat::User* user = userList->add_users();
            user->set_username(userName);
        }
    }
    {
        std::lock_guard<std::mutex> lock(info->responsesMutex);
        info->responses->push(response);
    }
    info->condition.notify_all();
}

void updateStatus(std::string userName, chat::Request request, ClientInfo* info, int* status) {
    if (!request.has_update_status()) {
        std::cerr << "No se especificó el nuevo estado." << std::endl;
    }
    std::cout << "Nuevo estado de " << userName << ": " << request.update_status().new_status() << std::endl;
    int newstatus = request.update_status().new_status();
    status = &newstatus;
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients[userName]["status"] = request.update_status().new_status();
    }
    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (request.update_status().new_status() == chat::UserStatus::OFFLINE) {
            auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
            if (it != onlineUsers.end()) {
                onlineUsers.erase(it);
            }
        } else {
            auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
            if (it == onlineUsers.end()) {
                onlineUsers.push_back(userName);
                info->itsNotOffline.notify_all();
            }
        }
    }
    chat::Response response;
    response.set_operation(chat::UPDATE_STATUS);
    response.set_status_code(chat::OK);
    response.set_message("Status updated.");
    std::cout << "Respuesta enviada. Update" << std::endl;
    {
        std::lock_guard<std::mutex> lock(info->responsesMutex);
        info->responses->push(response);
    }
    info->condition.notify_all();
}

void* handleClient(void* arg) {
    ClientInfo* info = static_cast<ClientInfo*>(arg);
    int status = chat::UserStatus::ONLINE;
    std::string userName;
    char buffer[4096];

    while (true) {
        ssize_t bytesRead = recv(info->socket, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0) {
            std::cerr << "Cliente desconectado. IP: " << info->ipAddress << std::endl;
            break;
        }
        std::cout << "Datos recibidos: " << std::string(buffer, bytesRead) << std::endl;
        chat::Request request;
        if (!request.ParseFromArray(buffer, bytesRead)) {
            std::cerr << "Error al parsear el mensaje." << std::endl;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(info->timerMutex);
            info->lastMessage = time(nullptr);
        }
        switch (request.operation()) {
            case chat::REGISTER_USER: {
                if (clients.find(request.register_user().username()) != clients.end()) {
                    std::cerr << "Usuario ya registrado. IP: " << info->ipAddress << std::endl;
                    break;
                }
                info->userName = request.register_user().username();
                {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    clients[request.register_user().username()]["status"] = status;
                }
                {
                    std::lock_guard<std::mutex> lock(onlineUsersMutex);
                    onlineUsers.push_back(request.register_user().username());
                }
                {
                    std::lock_guard<std::mutex> lock(clientsInfoMutex);
                    clientsInfo.push_back(info);
                }
                pthread_t timerThread;
                if (pthread_create(&timerThread, nullptr, handleTimerClient, info) != 0) {
                    std::cerr << "Error al crear el hilo del temporizador." << std::endl;
                } else {
                    pthread_detach(timerThread);
                }
                chat::Response response;
                response.set_operation(chat::REGISTER_USER);
                response.set_status_code(chat::OK);
                response.set_message("Usuario registrado correctamente.");
                {
                    std::lock_guard<std::mutex> lock(info->responsesMutex);
                    info->responses->push(response);
                }
                info->condition.notify_all();
                std::cout << "Usuario registrado. IP: " << info->ipAddress << std::endl;
                break;
            }
            case chat::SEND_MESSAGE: {
                sendMessage(&request, info, userName);
                break;
            }
            case chat::GET_USERS: {
                sendUsersList(info);
                break;
            }
            case chat::UPDATE_STATUS: {
                updateStatus(userName, request, info, &status);
                break;
            }
            case chat::UNREGISTER_USER: {
                {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    clients.erase(userName);
                }
                {
                    std::lock_guard<std::mutex> lock(onlineUsersMutex);
                    auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
                    if (it != onlineUsers.end()) {
                        onlineUsers.erase(it);
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(clientsInfoMutex);
                    auto it = std::remove_if(clientsInfo.begin(), clientsInfo.end(), [info](ClientInfo* clientInfo) {
                        return clientInfo == info;
                    });
                    clientsInfo.erase(it, clientsInfo.end());
                }
                info->connected = false;
                close(info->socket);
                delete info;
                return nullptr;
            }
            default:
                std::cerr << "Operación desconocida." << std::endl;
                break;
        }
    }
    info->connected = false;
    close(info->socket);
    delete info;
    return nullptr;
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Error al crear el socket del servidor." << std::endl;
        return 1;
    }
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    int optval = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Error al enlazar el socket del servidor." << std::endl;
        close(serverSocket);
        return 1;
    }
    if (listen(serverSocket, 10) == -1) {
        std::cerr << "Error al escuchar en el socket del servidor." << std::endl;
        close(serverSocket);
        return 1;
    }
    std::cout << "Servidor en espera de conexiones..." << std::endl;
    pthread_t messageThread;
    if (pthread_create(&messageThread, nullptr, handleThreadMessages, nullptr) != 0) {
        std::cerr << "Error al crear el hilo para manejar mensajes." << std::endl;
        close(serverSocket);
        return 1;
    }
    pthread_detach(messageThread);
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientAddrSize = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);
        if (clientSocket == -1) {
            std::cerr << "Error al aceptar la conexión del cliente." << std::endl;
            continue;
        }
        ClientInfo* info = new ClientInfo();
        info->socket = clientSocket;
        info->ipAddress = inet_ntoa(clientAddr.sin_addr);
        info->responses = new std::queue<chat::Response>();
        info->connected = true;
        info->lastMessage = time(nullptr);
        std::cout << "Cliente conectado. IP: " << info->ipAddress << std::endl;
        pthread_t clientThread;
        if (pthread_create(&clientThread, nullptr, handleClient, info) != 0) {
            std::cerr << "Error al crear el hilo para manejar el cliente." << std::endl;
            close(clientSocket);
            delete info;
        } else {
            pthread_detach(clientThread);
        }
    }
    close(serverSocket);
    return 0;
}