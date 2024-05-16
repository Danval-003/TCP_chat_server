g++ -o ./exec/server server.cpp ./proto/chat.pb.cc -pthread -lprotobuf -static-libstdc++
./exec/server