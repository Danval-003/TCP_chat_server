g++ -o ./exec/server server.cpp ./proto/chat.pb.cc ./src/sendFunction.cpp -pthread -lprotobuf -static-libstdc++ -Iinclude
./exec/server