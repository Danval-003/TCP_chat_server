g++ -o ./exec/client client.cpp ./proto/chat.pb.cc ./src/sendFunction.cpp -pthread -lprotobuf -static-libstdc++ -Iinclude
./exec/client $1 $2 $3