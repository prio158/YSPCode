cmake_minimum_required(VERSION 3.17)
project(SDL_DEMO)

# 设置变量
# set(LIB_PATH ../lib)

# 1导入头文件的文件夹，它会该文件夹中的所有头文件导入
set(SDL_DIR /opt/homebrew/Cellar/ffmpeg/5.1.2_4/)
include_directories(${SDL_DIR}/include/)
message("include:"${SDL_DIR}/include/)
link_directories(${SDL_DIR}/lib/)
message("lib:"${SDL_DIR}/lib/)

add_definitions("-Wall -g")


# add_executable(SDL_DEMO ../src/demo1.cpp)
add_executable(DEMO ../main2.c )

target_link_libraries(DEMO avformat avcodec avdevice avfilter avutil postproc swresample swscale)

