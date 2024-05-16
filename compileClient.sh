g++ -o ./exec/client client.cpp ./proto/chat.pb.cc -pthread -lprotobuf -static-libstdc++
./exec/client