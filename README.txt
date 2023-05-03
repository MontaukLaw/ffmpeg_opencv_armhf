cmake -S . -B build
cmake --build build
./build/ffmpeg_rtmp


交叉编译:
cd build
cmake ..
make

或者老办法
cmake -S . -B build
cmake --build build
