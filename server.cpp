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
    int socket;            // Socket asociado con el cliente
    std::string ipAddress; // Dirección IP del cliente en formato legible
    std::queue<chat::Response>* responses; // Cola de respuestas para el cliente
    std::mutex responsesMutex; // Mutex para prevenir el acceso simultáneo a la cola de respuestas
    bool* connected; // Indica si el cliente está conectado
    std::condition_variable condition; // Var condition to notify the main thread that there are new messages
    std::mutex notIsEmtpyMutex; // Mutex to prevent simultaneous access to the notIsEmpty variable
};


using json = nlohmann::json;

// Estructura para almacenar información de los clientes
json clients;
// Estructura para prevenir el acceso simultáneo a la estructura de clientes
std::mutex clientsMutex;
// Bandera para indicar al hilo de clientes que hay nuevos clientes
bool newClients = false;
// Queue to store the incoming message from the server (FIFO)
std::queue<chat::Response> messages;
// Mutex to prevent simultaneous access to the messages queue
std::mutex messagesMutex;
// Mutex to prevent simultaneous access to the messages queue
std::mutex messagesMutex2;
// Create a conditon variable to notify the main thread that there are new messages
std::condition_variable messagesCondition;
// Create a vector to store the info to send to the clients
std::vector<ClientInfo*> clientsInfo;
// Mutex to prevent simultaneous access to the clients info vector
std::mutex clientsInfoMutex;
// Save online users on a json list
json onlineUsers;



void* handleThreadMessages(void* arg) {
    while (1) {
        std::unique_lock<std::mutex> lock(messagesMutex2);
        messagesCondition.wait(lock, [] { return !messages.empty(); });

        chat::Response message = messages.front();
        std::cout << "Mensaje enviado a todos los clientes." << std::endl;
        messages.pop();
        lock.unlock();

        // Enviar el mensaje a todos los clientes
        clientsInfoMutex.lock();
        for (ClientInfo* info : clientsInfo) {
            info->responsesMutex.lock();
            info->responses->push(message);
            info->responsesMutex.unlock();
            // Notify the response thread that there are new messages
            info->condition.notify_all();
        }
        clientsInfoMutex.unlock();
    }
}

void sendMessage(chat::Request* request, ClientInfo* info, std::string sender){
    chat::Response response_message;
    response_message.set_operation(chat::INCOMING_MESSAGE);
    response_message.set_status_code(chat::OK);
    response_message.set_message("Mensaje enviado exitosamente.");
    response_message.mutable_incoming_message()->set_sender(sender);
    response_message.mutable_incoming_message()->set_content(request->send_message().content());
    messagesMutex.lock();
    messages.push(response_message);
    messagesMutex.unlock();
    messagesCondition.notify_one();
}


void sendUsersList(ClientInfo* info){
    chat::Response response;
    response.set_operation(chat::GET_USERS);
    response.set_status_code(chat::OK);
    response.set_message("Lista de usuarios.");
    chat::UserListResponse* userList = response.mutable_user_list();
    userList->set_type(chat::ALL);
    for (int i = 0; i < onlineUsers.size(); i++) {
        chat::User* user = userList->add_users();
        user->set_username(onlineUsers[i]);
    }
    info->responsesMutex.lock();
    info->responses->push(response);
    info->responsesMutex.unlock();
    //Notify the response thread that there are new messages
    info->condition.notify_all();

}


void* handleListenClient(void* arg) {
    ClientInfo* info = (ClientInfo*)arg;
    int clientSocket = info->socket;

    // Recive the request
    chat::Request first_request;
    // First request are the user name
    int status = getRequest(&first_request, clientSocket);
    if (status == -1) {
        std::cerr << "Error al recibir la solicitud." << std::endl;
        return NULL;
    }

    if (status == 2) {
        std::cerr << "Cliente desconectado" << std::endl;
        return NULL;
    }

    // Verify the operation are login
    if (first_request.operation() != chat::REGISTER_USER) {
        std::cerr << "Error al recibir la solicitud." << std::endl;
        return NULL;
    }

    // Get the user name
    std::string userName = first_request.register_user().username();
    std::cout << "Usuario: " << userName << std::endl;

    first_request.Clear();
    json client;
    client["ip"] = info->ipAddress;
    client["socket"] = clientSocket;
    client["status"] = "online";

    // Add the user to the clients list, but first verify if the user is already in the list
    clientsMutex.lock();
    if (clients.find(userName) != clients.end()) {
        // Verify if ip its the same
        if (clients[userName]["ip"] != info->ipAddress) {
            clientsMutex.unlock();
            chat::Response badResponse;
            badResponse.set_operation(chat::REGISTER_USER);
            badResponse.set_status_code(chat::BAD_REQUEST);
            // Send the message to the client on English
            badResponse.set_message("User already registered.");
            info->responsesMutex.lock();
            info->responses->push(badResponse);
            info->responsesMutex.unlock();
            info->condition.notify_all();
            return NULL;
        } else {
            clientsMutex.unlock();
            chat::Response goodResponse;
            goodResponse.set_operation(chat::REGISTER_USER);
            goodResponse.set_status_code(chat::OK);
            // Send the message to the client on English to success with login with existing user
            goodResponse.set_message("Successfully logged in.");
            info->responsesMutex.lock();
            info->responses->push(goodResponse);
            info->responsesMutex.unlock();
            info->condition.notify_all();
        }
    } else{
        clients[userName] = client;
        clientsMutex.unlock();
        chat::Response goodResponse;
        goodResponse.set_operation(chat::REGISTER_USER);
        goodResponse.set_status_code(chat::OK);
        // Send the message to the client on English to success with login with new user
        goodResponse.set_message("Successfully registered.");
        info->responsesMutex.lock();
        info->responses->push(goodResponse);
        info->responsesMutex.unlock();
        info->condition.notify_all();
    }

    // Save new online user on onlineUsers list
    onlineUsers.push_back(userName);

    std::cout<<"Nuevo usuario"<< userName<< clientSocket<<std::endl;

    while (info->connected) {
        chat::Request request;
        int status = getRequest(&request, clientSocket);
        if (status == -1) {
            // Fail to recive the request, cerr message on english
            std::cerr << "Fail to recive the request." << std::endl;
            break;
        }
        if (status == 2) {
            std::cerr << userName << " Log out." << std::endl;
            break;
        }

        switch (request.operation())
        {
        case chat::SEND_MESSAGE:
            sendMessage(&request, info, userName);
            break;

        case chat::GET_USERS:
            // Verify if username in request is empty
            sendUsersList(info);
            break;
        default:
            break;
        }
    }

    // Delete from the online users list if exists
    for (int i = 0; i < onlineUsers.size(); i++) {
        if (onlineUsers[i] == userName) {
            onlineUsers.erase(i);
            break;
        }
    }

    // Cerrar el socket del cliente
    close(clientSocket);

    return NULL; // Agregar declaración de retorno
}


void* handleResponseClient(void* arg){
    ClientInfo* info = (ClientInfo*)arg;
    int clientSocket = info->socket;

    while (info->connected || !info->responses->empty()) {
        chat::Response response;
        // Verify if the response queue is empty, if is empty wait for a new message
        std::unique_lock<std::mutex> lock(info->notIsEmtpyMutex);
        info->condition.wait(lock, [info] { return !info->responses->empty() || !(*info->connected); });

        info->responsesMutex.lock();
        if (!info->responses->empty()) {
            response = info->responses->front();
            info->responses->pop();
        }
        info->responsesMutex.unlock();
        int status = sendResponse(&response, clientSocket);
        if (status == -1) {
            std::cerr << "Error al enviar la respuesta." << std::endl;
            break;
        }
        std:: cout << "Respuesta enviada." << std::endl;
        std::cout << "Operación: " << response.operation() << std::endl;
        std::cout << "Ip"<<info->ipAddress  << std::endl;
        std::cout << "Ip"<<info->socket  << std::endl;
    }

    // Cerrar el socket del cliente
    close(clientSocket);

    return NULL; // Agregar declaración de retorno
}


int main() {
    // Crear el socket del servidor
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Error al crear el socket del servidor." << std::endl;
        return 1;
    }

    // Configurar la dirección del servidor
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(4000); // Puerto 4000

    // Vincular el socket a la dirección del servidor
    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        std::cerr << "Error al vincular el socket." << std::endl;
        close(serverSocket);
        return 1;
    }

    // Escuchar conexiones entrantes
    if (listen(serverSocket, 5) == -1) {
        std::cerr << "Error al escuchar conexiones entrantes." << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "Servidor TCP iniciado. Esperando conexiones..." << std::endl;

    while (1) {
        // Aceptar conexiones entrantes
        sockaddr_in clientAddress{};
        socklen_t clientAddressLength = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
        if (clientSocket == -1) {
            std::cerr << "Error al aceptar la conexión entrante." << std::endl;
            close(serverSocket);
            return 1;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddress.sin_addr), clientIP, INET_ADDRSTRLEN);

        ClientInfo clientInfo;
        clientInfo.socket = clientSocket;
        clientInfo.ipAddress = clientIP;
        clientInfo.responses = new std::queue<chat::Response>();
        clientInfo.connected = new bool(true);

        std::cout << "Cliente conectado. Desde "<<clientInfo.ipAddress<<":" <<clientInfo.socket<<"."<< std::endl;
        std::cout << BUFFER_SIZE << std::endl;



        // Crear hilo para manejar el cliente
        pthread_t clientThread;
        if (pthread_create(&clientThread, NULL, handleListenClient, (void*)&clientInfo) != 0) {
            std::cerr << "Error al crear el hilo para el cliente." << std::endl;
            close(clientSocket);
            continue; // Continuar aceptando nuevas conexiones
        }

        pthread_t responseThread;
        if (pthread_create(&responseThread, NULL, handleResponseClient, (void*)&clientInfo) != 0) {
            std::cerr << "Error al crear el hilo para el cliente." << std::endl;
            close(clientSocket);
            continue; // Continuar aceptando nuevas conexiones
        }

        pthread_t messageThread;
        if (pthread_create(&messageThread, NULL, handleThreadMessages, NULL) != 0) {
            std::cerr << "Error al crear el hilo para el cliente." << std::endl;
            close(clientSocket);
            continue; // Continuar aceptando nuevas conexiones
        }

        // Save the client info
        clientsInfoMutex.lock();
        clientsInfo.push_back(&clientInfo);
        clientsInfoMutex.unlock();
    }

    // Cerrar el socket del servidor (nunca llega aquí en un bucle infinito)
    close(serverSocket);

    return 0;
}
