cmake_minimum_required(VERSION 3.25)

project(dashcam VERSION 0.0.1 DESCRIPTION "dashcam" LANGUAGES CXX)
# C++ Settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Camera backend selection: ON for Raspberry Pi with Pi Camera Module (requires rpicam-vid),
# OFF (default) for USB camera development on a desktop.
option(USE_PI_CAMERA "Build with Pi Camera Module support (rpicam-vid)" OFF)
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

# Tests
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    file(GLOB TEST_SOURCES "tests/*_test.cpp")
    foreach(TEST_SRC ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_sources(${TEST_NAME} PRIVATE ${SOURCES})
        target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(${TEST_NAME} PRIVATE OpenSSL::SSL ${OpenCV_LIBS})
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()
