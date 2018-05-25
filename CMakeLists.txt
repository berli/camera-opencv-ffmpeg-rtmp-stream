cmake_minimum_required(VERSION 3.5)
project(rtmp-stream)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SUPPRESS_REGENERATION true)
SET(CMAKE_BUILD_TYPE "Debug") 
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

find_package(OpenCV REQUIRED)

set(INC_DIRS ${PROJECT_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})
set(LIBS ${OpenCV_LIBS} -lavcodec -lavformat -lavutil -lswscale -lavfilter -lavutil -lavdevice -lpthread )
#set(LIBS ${OpenCV_LIBS} /usr/local/lib/libavcodec.a /usr/local/lib/libavformat.a -l/usr/local/lib/libavutil.a -lswscale -lavfilter -lavdevice -lpthread )

set(PROJECT_INCLUDE_DIRS ${INC_DIRS})

add_executable(rtmp-stream ${PROJECT_SOURCE_DIR}/src/rtmp-stream.cpp)

target_include_directories(rtmp-stream PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(rtmp-stream ${LIBS})
