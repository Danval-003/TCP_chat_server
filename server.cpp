#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <cstring>

void handleClient(int clientSocket) {
    // Aquí puedes realizar cualquier lógica adicional para comunicarte con el cliente

    while (1)
    {
        // Recibir datos del cliente
        char buffer[1024] = {0};
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0) {
            std::cerr << "Se desconecto el cliente" << std::endl;
            break;
        }

        std::cout << "Cliente: " << buffer << std::endl;

        // Enviar datos al cliente
        const char* message = "Hola, cliente!";
        if (send(clientSocket, message, strlen(message), 0) < 0) {
            std::cerr << "Error al enviar datos al cliente." << std::endl;
            break;
        }
    }
    

    // Cerrar el socket del cliente
    close(clientSocket);
}

int main() {
    // Crear el socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Error al crear el socket." << std::endl;
        return 1;
    }

    // Configurar la dirección del servidor
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(8080); // Puerto 8080

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

        std::cout << "Cliente conectado." << std::endl;

        // Crear un hilo para manejar la conexión del cliente
        std::thread clientThread(handleClient, clientSocket);
        clientThread.detach();
    }

    // Cerrar el socket del servidor
    close(serverSocket);

    return 0;
}