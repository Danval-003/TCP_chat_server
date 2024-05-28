g++ -o ../exec/test test.cpp ../proto/chat.pb.cc ../src/sendFunction.cpp -pthread -lprotobuf -static-libstdc++ -I../include
../exec/test