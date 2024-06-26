#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
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
#include <time.h>
#include <vector>

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
// Define Timeout with 5 seconds with time
constexpr double TIMEOUT = 60.0;

json clients;
std::mutex clientsMutex;
std::queue<chat::Response> messages;
std::mutex messagesMutex;
std::condition_variable messagesCondition;
std::vector<ClientInfo*> clientsInfo;
std::mutex clientsInfoMutex;
std::mutex onlineUsersMutex;
std::mutex onlineIpsMutex;
json onlineUsers;
std::vector<std::string> onlineIps;

void* handleTimerClient(void* arg){
    ClientInfo* info = static_cast<ClientInfo*>(arg);
    int oldtype;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype);
    try{
        while (info->connected) {
        // Wait if user is offline
        {
            std::unique_lock<std::mutex> lock(info->timerMutex);
            info->itsNotOffline.wait(lock, [info] { return info->connected && clients[info->userName]["status"] != chat::UserStatus::OFFLINE; });
        }
        double seconds = difftime(time(nullptr), info->lastMessage);
        if (seconds >= TIMEOUT) {
            // Change status to offline
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients[info->userName]["status"] = chat::UserStatus::OFFLINE;
                // Update json file with clients
                std::ofstream file("clients.json");
                file << clients.dump(4);
                file.close();
            }
            // Remove user from online users
            {
                std::lock_guard<std::mutex> lock(onlineUsersMutex);
                auto it = std::find(onlineUsers.begin(), onlineUsers.end(), info->userName);
                if (it != onlineUsers.end()) {
                    onlineUsers.erase(it);
                }
            }
        }
    }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cout << "Error: " << e.what() << std::endl;
        // Try to create a new thread
        pthread_t timerThread;
        if (pthread_create(&timerThread, nullptr, handleTimerClient, (void*)info) != 0) {
            std::cerr << "Error al crear el hilo del temporizador." << std::endl;
        }
    }
    return nullptr;

}


void* handleThreadMessages(void* arg) {
    try {
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
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cout << "Error: " << e.what() << std::endl;
        // Try to create a new thread
        pthread_t messageThread;
        if (pthread_create(&messageThread, nullptr, handleThreadMessages, nullptr) != 0) {
            std::cerr << "Error al crear el hilo para manejar mensajes." << std::endl;
        }
    }
    return nullptr;
}

void sendMessage(chat::Request* request, ClientInfo* info, const std::string& sender) {    
    // Verify if reciper not empty and If not exists, send message to all
    std::string reciper = request->send_message().recipient();
    // Print message to console
    if (reciper.empty()) {
        chat::Response response;
        response.set_operation(chat::INCOMING_MESSAGE);
        response.set_status_code(chat::OK);
        response.set_message("Message sent.");
        chat::IncomingMessageResponse* msg = response.mutable_incoming_message();
        msg->set_content(request->send_message().content());
        msg->set_sender(sender);
        msg->set_type(chat::BROADCAST);
        {
            std::lock_guard<std::mutex> lock(messagesMutex);
            messages.push(response);
        }
        messagesCondition.notify_all();
    } else {
        // Verify if reciper exists
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

        // Verify if reciper is not offline
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            if (clients[reciper]["status"] == chat::UserStatus::OFFLINE) {
                chat::Response response;
                response.set_operation(chat::SEND_MESSAGE);
                response.set_status_code(chat::BAD_REQUEST);
                response.set_message("User is offline.");
                {
                    std::lock_guard<std::mutex> lock(info->responsesMutex);
                    info->responses->push(response);
                }
                info->condition.notify_all();
                return;
            }
        }

        // Send message to reciper
        chat::Response response;
        response.set_operation(chat::INCOMING_MESSAGE);
        response.set_status_code(chat::OK);
        response.set_message("Message sent.");
        chat::IncomingMessageResponse* msg = response.mutable_incoming_message();
        msg->set_content(request->send_message().content());
        msg->set_sender(sender);
        msg->set_type(chat::DIRECT);
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

    // Send response
    chat::Response response;
    response.set_operation(chat::SEND_MESSAGE);
    response.set_status_code(chat::OK);
    response.set_message("Message sent.");
    {
        std::lock_guard<std::mutex> lock(info->responsesMutex);
        info->responses->push(response);
    }
    info->condition.notify_all();
}

void sendUsersList(ClientInfo* info) {
    chat::Response response;
    response.set_operation(chat::GET_USERS);
    response.set_status_code(chat::OK);
    response.set_message("Lista de usuarios.");
    chat::UserListResponse* userList = response.mutable_user_list();
    userList->set_type(chat::ALL);

    // Get all users arent offline, from clients
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            if (it.value()["status"] != chat::UserStatus::OFFLINE) {
                std::string username = it.key();
                chat::User* user = userList->add_users();
                user->set_username(username);
                user->set_status(it.value()["status"]);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(info->responsesMutex);
        info->responses->push(response);
    }

    info->condition.notify_all();
}

void userInfo(std::string userName, ClientInfo* info){
    // Client
    std::lock_guard<std::mutex> lock(clientsMutex);
    // Verify if userName exists into clients
    if (clients.find(userName) == clients.end()) {
        chat::Response response;
        response.set_operation(chat::GET_USERS);
        response.set_status_code(chat::BAD_REQUEST);
        response.set_message("User not found.");
        {
            std::lock_guard<std::mutex> lock(info->responsesMutex);
            info->responses->push(response);
        }
        info->condition.notify_all();
        return;
    }


    std::string ip = clients[userName]["ip"];
    std::string username = userName+" (" + ip + ")";
    chat::Response response;
    response.set_operation(chat::GET_USERS);
    response.set_status_code(chat::OK);
    response.set_message("Lista de usuarios.");
    chat::UserListResponse* userList = response.mutable_user_list();
    userList->set_type(chat::SINGLE);
    chat::User* user = userList->add_users();
    user->set_username(username);
    user->set_status(clients[userName]["status"]);
    {
        std::lock_guard<std::mutex> lock(info->responsesMutex);
        info->responses->push(response);
    }
    info->condition.notify_all();
}

void updateStatus(std::string userName, chat::Request request, ClientInfo* info){
    // Verify if exist status in request
    if (!request.has_update_status()) {
        std::cerr << "No se especificó el nuevo estado." << std::endl;
    }
    // Update status
    int newstatus = request.update_status().new_status();

    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients[userName]["status"] = request.update_status().new_status();
        // Update json file with clients
        std::ofstream file("clients.json");
        file << clients.dump(4);
        file.close();
    }
    // If new status not online or busy, remove user from online users. And if new status is online or busy, add user to online users.
    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (request.update_status().new_status() == chat::UserStatus::OFFLINE) {
            auto it = std::find(onlineUsers.begin(), onlineUsers.end(), info->userName);
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
    

    // Send response
    chat::Response response;
    response.set_operation(chat::UPDATE_STATUS);
    response.set_status_code(chat::OK);
    response.set_message("Status updated.");

    {
        std::lock_guard<std::mutex> lock(info->responsesMutex);
        info->responses->push(response);
    }
    info->condition.notify_all();
}



void* handleResponseClient(void* arg) {
    ClientInfo* info = static_cast<ClientInfo*>(arg);
    int clientSocket = info->socket;
    int oldtype;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype);

    try {
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
        } else {
            lock.unlock();
        }
        }
    } 
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        // Try to create a new thread
        pthread_t responseThread;
        if (pthread_create(&responseThread, nullptr, handleResponseClient, (void*)info) != 0) {
            std::cerr << "Error al crear el hilo de respuesta." << std::endl;
        }
    }

    close(clientSocket);
    return nullptr;
}


void* handleListenClient(void* arg) {
    ClientInfo* info = static_cast<ClientInfo*>(arg);
    int clientSocket = info->socket;

    chat::Request first_request;
    int status_0 = getRequest(&first_request, clientSocket);
    if (status_0 == -1 || status_0 == 2) {
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
    client["status"] = chat::UserStatus::ONLINE;
    int status = chat::UserStatus::ONLINE;
    

        // Create response thread
    pthread_t responseThread;
    if (pthread_create(&responseThread, nullptr, handleResponseClient, (void*)info) != 0) {
        std::cerr << "Error al crear el hilo de respuesta." << std::endl;
        return nullptr;
    }


    // Verify if ip are not exist in online IPs
    {
        std::lock_guard<std::mutex> lock(onlineIpsMutex);
        if (std::find(onlineIps.begin(), onlineIps.end(), info->ipAddress) != onlineIps.end()) {
            chat::Response badResponse;
            badResponse.set_operation(chat::REGISTER_USER);
            badResponse.set_status_code(chat::BAD_REQUEST);
            badResponse.set_message("IPs already in use.");
            std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
            info->responses->push(badResponse);
            info->condition.notify_all();
            info->connected = false;
            return nullptr;
        }
    }


    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        if (clients.find(userName) != clients.end()) {
            // Print clients list
            std::cout << clients.dump(4) << std::endl;
            std::cout << "ip:" << clients[userName]["ip"] << "info->ipAddress:" << info->ipAddress << std::endl;

            if (clients[userName]["ip"] != info->ipAddress) {
                chat::Response badResponse;
                badResponse.set_operation(chat::REGISTER_USER);
                badResponse.set_status_code(chat::BAD_REQUEST);
                badResponse.set_message("User already registered.");
                std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
                info->responses->push(badResponse);
                info->condition.notify_all();
                info->connected = false;
                return nullptr;
            } else {
                chat::Response goodResponse;
                goodResponse.set_operation(chat::REGISTER_USER);
                goodResponse.set_status_code(chat::OK);
                goodResponse.set_message("Successfully logged in.");
                status = clients[userName]["status"];
                std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
                info->responses->push(goodResponse);
                info->condition.notify_all();
                // Add IP
                {
                    std::lock_guard<std::mutex> lock(onlineIpsMutex);
                    onlineIps.push_back(info->ipAddress);
                } 
            }
        } else {

            // Verify if this ip not into clients
            {
                for (auto it = clients.begin(); it != clients.end(); ++it) {
                    if (it.value()["ip"] == info->ipAddress) {
                        chat::Response badResponse;
                        badResponse.set_operation(chat::REGISTER_USER);
                        badResponse.set_status_code(chat::BAD_REQUEST);
                        badResponse.set_message("User already has other username associated.");
                        std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
                        info->responses->push(badResponse);
                        info->condition.notify_all();
                        info->connected = false;
                        return nullptr;
                    }
                }
            }


            clients[userName] = client;
            chat::Response goodResponse;
            goodResponse.set_operation(chat::REGISTER_USER);
            goodResponse.set_status_code(chat::OK);
            goodResponse.set_message("Successfully registered.");
            std::lock_guard<std::mutex> responsesLock(info->responsesMutex);
            info->responses->push(goodResponse);
            info->condition.notify_all();
            // Update json file with clients
            std::ofstream file("clients.json");
            file << clients.dump(4);
            file.close();
            // Add ip to online IPs
            {
                std::lock_guard<std::mutex> lock(onlineIpsMutex);
                onlineIps.push_back(info->ipAddress);
            }
        }
    }

    info->userName = userName;

    {
        std::lock_guard<std::mutex> lock(clientsInfoMutex);
        clientsInfo.push_back(info);
    }

    // Create timer
    {
        std::lock_guard<std::mutex> lock(info->timerMutex);
        info->lastMessage = time(nullptr);
    }

    // If is not offline, add user to online users
    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (status != chat::UserStatus::OFFLINE) {
            onlineUsers.push_back(userName);
            info->itsNotOffline.notify_all();
        }
    }

    bool unregister = false;

    pthread_t timerThread;
    if (pthread_create(&timerThread, nullptr, handleTimerClient, (void*)info) != 0) {
        std::cerr << "Error al crear el hilo del temporizador." << std::endl;
        return nullptr;
    }


    try {
        chat::Request request;
        while (info->connected) {
            request.Clear();
            int status2 = getRequest(&request, clientSocket);
            if (status2 == -1 || status2 == 2) {
                std::cerr << userName << " se desconectó." << std::endl;
                    // Delete from online IPs
                    {
                        std::lock_guard<std::mutex> lock(onlineIpsMutex);
                        auto it = std::find(onlineIps.begin(), onlineIps.end(), info->ipAddress);
                        if (it != onlineIps.end()) {
                            onlineIps.erase(it);
                        }
                    }
                break;
            }

            // Update last message time
            {
                std::lock_guard<std::mutex> lock(info->timerMutex);
                info->lastMessage = time(nullptr);
            }

            // change status to original status
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients[userName]["status"] = status ;
                if (status != chat::UserStatus::OFFLINE) {
                    info->itsNotOffline.notify_all();
                }
                // Update json file with clients
                std::ofstream file("clients.json");
                file << clients.dump(4);
                file.close();
            }

            // If status is offline, remove user from online users, else add user to online users
            {
                std::lock_guard<std::mutex> lock(onlineUsersMutex);
                if (status == chat::UserStatus::OFFLINE) {
                    auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
                    if (it != onlineUsers.end()) {
                        onlineUsers.erase(it);
                    }
                } else {
                    auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
                    if (it == onlineUsers.end()) {
                        onlineUsers.push_back(userName);
                    }
                }
            }

            std::cout<< "Operation: " << request.operation()<< "username:"<< userName << std::endl;

            switch (request.operation()) {
                case chat::SEND_MESSAGE:
                    sendMessage(&request, info, userName);
                    break;
                case chat::GET_USERS:
                    std::cout << "Get users "<< request.get_users().username()<< std::endl;
                    // Verify if username in request is empty
                    if (request.get_users().username().empty()) {
                        sendUsersList(info);
                    } else {
                        userInfo(request.get_users().username(), info);
                    }
                    break;
                case chat::UPDATE_STATUS:
                    updateStatus(userName, request, info);
                    // Obtain on status, status from clients
                    {
                        std::lock_guard<std::mutex> lock(clientsMutex);
                        status = clients[userName]["status"];
                    }
                    break;

                case chat::UNREGISTER_USER:
                    unregister = true;
                    info->connected = false;
                    break;
                default:
                    break;
            }
        }

    }
    catch (const std::exception& e) {
        chat::Response response;
        response.set_status_code(chat::INTERNAL_SERVER_ERROR);
        response.set_message(e.what());
        {
            std::lock_guard<std::mutex> lock(info->responsesMutex);
            info->responses->push(response);
        }
        info->condition.notify_all();
        std::cout << "Error: " << e.what() << std::endl;
        info->connected = false;
    }

    if (unregister){

        // Response to client
        chat::Response response;
        response.set_operation(chat::UNREGISTER_USER);
        response.set_status_code(chat::OK);
        response.set_message("Successfully unregistered.");
        {
            std::lock_guard<std::mutex> lock(info->responsesMutex);
            info->responses->push(response);
        }
        info->condition.notify_all();
        pthread_cancel(timerThread);
        // unit threads
        pthread_join(responseThread, nullptr);
        pthread_join(timerThread, nullptr);



        // Delete user from clients info
        {
            std::lock_guard<std::mutex> lock(clientsInfoMutex);
            auto it = std::find(clientsInfo.begin(), clientsInfo.end(), info);
            if (it != clientsInfo.end()) {
                clientsInfo.erase(it);
            }
        }

        // Delete user from online users
        {
            std::lock_guard<std::mutex> lock(onlineUsersMutex);
            auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
            if (it != onlineUsers.end()) {
                onlineUsers.erase(it);
            }
        }

        // Delete user from clients
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            // Find client username
            auto it = clients.find(userName);
            if (it != clients.end()) {
                clients.erase(it);
            }

            // Update json file with clients
            std::ofstream file("clients.json");
            file << clients.dump(4);
            file.close();
        }

                // Delete Ip from ips online (vector)
        {
            std::lock_guard<std::mutex> lock(onlineIpsMutex);
            auto it = std::find(onlineIps.begin(), onlineIps.end(), info->ipAddress);
            if (it != onlineIps.end()) {
                onlineIps.erase(it);
            }
        }

        close(clientSocket);
    }
    else{
         try {

                // Change status to offline
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients[userName]["status"] = chat::UserStatus::OFFLINE;
                // Update json file with clients
                std::ofstream file("clients.json");
                file << clients.dump(4);
                file.close();
            }

            // Wait for the response thread to finish
            pthread_join(responseThread, nullptr);
            // Force the timer thread to finish
            info->connected = false;
            info->itsNotOffline.notify_all();
            pthread_join(timerThread, nullptr);


            // Disconnect user
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.erase(userName);
                // Update json file with clients
                std::ofstream file("clients.json");
                file << clients.dump(4);
                file.close();
            }

            // Delete user from online users
            {
                std::lock_guard<std::mutex> lock(onlineUsersMutex);
                auto it = std::find(onlineUsers.begin(), onlineUsers.end(), userName);
                if (it != onlineUsers.end()) {
                    onlineUsers.erase(it);
                }
            }

            // Delete user from clients info
            {
                std::lock_guard<std::mutex> lock(clientsInfoMutex);
                auto it = std::find(clientsInfo.begin(), clientsInfo.end(), info);
                if (it != clientsInfo.end()) {
                    clientsInfo.erase(it);
                }
            }
            // Delete Ip from ips online (vector)
            {
                std::lock_guard<std::mutex> lock(onlineIpsMutex);
                auto it = std::find(onlineIps.begin(), onlineIps.end(), info->ipAddress);
                if (it != onlineIps.end()) {
                    onlineIps.erase(it);
                }
            }

            close(clientSocket);
            }
            catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                std::cout << "Error: " << e.what() << std::endl;
                // Try to create a new thread
                pthread_t clientThread;
                if (pthread_create(&clientThread, nullptr, handleListenClient, (void*)info) != 0) {
                    std::cerr << "Error al crear el hilo para el cliente." << std::endl;
                }
            }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    int port;
    // Obtain from args port to server
    if (argc != 2) {
        std::cerr << "Uso: " << argv[0] << " <puerto>" << std::endl;
        return 1;
    }

    port = std::stoi(argv[1]);

    // Obtain last serverSocket if exist from file
    json lastServer;
    std::ifstream lastFile("server.json");
    if (lastFile.is_open()) {
        lastFile >> lastServer;
        lastFile.close();
    }

    if (!lastServer.empty()) {
        close(lastServer["socket"]);
    }


    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    

    if (serverSocket == -1) {
        std::cerr << "Error al crear el socket del servidor." << std::endl;
        return 1;
    }

    // Save server socket into a json
    json server;
    server["socket"] = serverSocket;

    // Save into a file
    std::ofstream file("server.json");
    file << server.dump(4);
    file.close();

    // Obtain clients from file
    std::ifstream clientsFile("clients.json");
    if (clientsFile.is_open()) {
        clientsFile >> clients;
        clientsFile.close();
    }
    

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

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

        pthread_t clientThread;
        if (pthread_create(&clientThread, nullptr, handleListenClient, (void*)clientInfo) != 0) {
            std::cerr << "Error al crear el hilo para el cliente." << std::endl;
            close(clientSocket);
            delete clientInfo;
            continue;
        }
    }

    close(serverSocket);
    return 0;
}