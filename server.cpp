#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include "chat.pb.h"
#include "constants.h"

struct ClientInfo {
    int socket;            // Socket asociado con el cliente
    std::string ipAddress; // Dirección IP del cliente en formato legible
};

void* handleClient(void* arg) {
    int clientSocket = *((int*)arg);
    printf("Cliente conectado\n");

    while (1) {

        // Recibir la solicitud
        char buffer[BUFFER_SIZE];
        if (recv(clientSocket, buffer, BUFFER_SIZE, 0) <= 0) {
            std::cerr << "Error al recibir la solicitud." << std::endl;
            break;
        }

        // Deserializar la solicitud
        chat::Request request;
        if (!request.ParseFromArray(buffer, BUFFER_SIZE)) {
            std::cerr << "Error al analizar la solicitud." << std::endl;
            break;
        }

        // Procesar la solicitud

        switch (request.operation())
        {
        case chat::SEND_MESSAGE: 
            std::cout << "Cliente: " << request.send_message().content() << std::endl;
            break;
        
        default:
            break;
        }

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
    serverAddress.sin_port = htons(PORT); // Puerto 4000

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

    while (true) {
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

        std::cout << "Cliente conectado. Desde "<<clientInfo.ipAddress<<":" <<clientInfo.socket<<"."<< std::endl;
        std::cout << BUFFER_SIZE << std::endl;


        // Crear hilo para manejar el cliente
        pthread_t clientThread;
        if (pthread_create(&clientThread, NULL, handleClient, (void*)&clientSocket) != 0) {
            std::cerr << "Error al crear el hilo para el cliente." << std::endl;
            close(clientSocket);
            continue; // Continuar aceptando nuevas conexiones
        }
    }

    // Cerrar el socket del servidor (nunca llega aquí en un bucle infinito)
    close(serverSocket);

    return 0;
}
