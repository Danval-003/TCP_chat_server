**🚀 Chat Application README 🚀**

## Overview
This project comprises a chat application implemented in C++, consisting of two main components: `client.cpp` and `server.cpp`. The communication between client and server is facilitated by a protocol named `chat.proto`, which is located in the `proto` folder of the project along with its compiled versions: `chat.pb.cc` and `chat.pb.h`. The application relies on Google's Protocol Buffers (protobuf), so ensure protobuf is installed for compilation.

In addition to the protocol, the project utilizes functions for sending and receiving data, defined in `sendFunction.cpp` and `sendFunction.h` within the `src` folder. It also integrates a JSON package for C++, which is included in the `include` folder.

## Installation and Compilation
To compile the project:
1. Navigate to the `proto` folder and compile the protocol using:
   ```
   protoc --cpp_out=. chat.proto
   ```

2. For compiling the client:
   - Use the provided script:
     ```
     ./compileClient.sh <username> <server_ip> <port>
     ```
   - Or manually compile using:
     ```
     g++ -o ./exec/client client.cpp ./proto/chat.pb.cc ./src/sendFunction.cpp -pthread -lprotobuf -static-libstdc++ -Iinclude
     ```
   - Execute the client:
     ```
     ./exec/client <username> <server_ip> <port>
     ```

3. For compiling the server:
   - Use the provided script:
     ```
     ./compileServer.sh <port_to_listen>
     ```
   - Or manually compile using:
     ```
     g++ -o ./exec/server server.cpp ./proto/chat.pb.cc ./src/sendFunction.cpp -pthread -lprotobuf -static-libstdc++ -Iinclude
     ```
   - Execute the server:
     ```
     ./exec/server <port_to_listen>
     ```

## Functionality
The chat application supports the following features:

- **User Registration**: Clients send their username to the server, which registers it along with the source IP address. Duplicate usernames from different addresses are not allowed. The server manages user sessions concurrently through multithreading.

- **User Logout**: When a user exits the chat client, the server detects this and removes the user's information from the list of connected users.

- **Connected Users List**: Clients can request the list of currently connected users from the server, which responds with the list of usernames.

- **User Information Retrieval**: Clients can request information (IP address) about a specific connected user.

- **Status Change**: Clients can switch between different statuses: ACTIVE, BUSY, and INACTIVE. The server can also assign the INACTIVE status to clients after a predetermined period of inactivity.

- **Broadcasting and Direct Messaging**: Users can communicate via general chat (broadcasting) or send direct messages to specific users.

🌟 Thank you for using our chat application! 🌟

Feel free to reach out if you have any questions or suggestions. Happy chatting! 😊🚀