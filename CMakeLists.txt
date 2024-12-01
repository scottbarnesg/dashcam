cmake_minimum_required(VERSION 3.28)

project(dashcam VERSION 0.0.1 DESCRIPTION "dashcam" LANGUAGES CXX)
# C++ Settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Build executable
file(GLOB SOURCES "src/*cpp")
add_executable(${PROJECT_NAME} run/dashcam.cpp)
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Link libraries
find_package(OpenSSL REQUIRED)
find_package(OpenCV REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL)
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})
