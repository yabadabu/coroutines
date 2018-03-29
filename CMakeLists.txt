cmake_minimum_required(VERSION 2.6.0 FATAL_ERROR)
project(Coroutines)
set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/output" CACHE PATH "project install prefix" FORCE)

message("[Coroutines] Generating " ${PROJECT_NAME} "...")
file(GLOB PROJECT_INC_FILES "coroutines/*.h" )
file(GLOB PROJECT_CPP_FILES "coroutines/*.cpp")
source_group (Inc FILES ${PROJECT_INC_FILES} ${PROJECT_CPP_FILES})

include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})
message("[Coroutines] \t INC_FILES: ${PROJECT_INC_FILES}")
message("[Coroutines] \t CPP_FILES: ${PROJECT_CPP_FILES}")

add_subdirectory ("${CMAKE_SOURCE_DIR}/coroutines")
add_subdirectory ("${CMAKE_SOURCE_DIR}/samples")
