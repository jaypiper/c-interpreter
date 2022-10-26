mkdir -p build
gcc testlib.c test$1.c -o build/test$1
./build/test$1