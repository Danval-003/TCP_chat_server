g++ -o server server.cpp chat.pb.cc -pthread -lprotobuf -static-libstdc++
./server
