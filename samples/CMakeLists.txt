project(Coroutines_TEST)

file(GLOB PROJECT_INC_FILES "*.h" )
file(GLOB PROJECT_CPP_FILES "*.cpp")

add_executable(testCoroutines ${PROJECT_CPP_FILES} ${PROJECT_INC_FILES})

include_directories("${CMAKE_SOURCE_DIR}/Coroutines")

IF(WIN32)
target_link_libraries(testCoroutines Coroutines_LIB)
ELSEIF(APPLE)
target_link_libraries(testCoroutines Coroutines_LIB)
ELSE ()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall")
target_link_libraries(testCoroutines Coroutines_LIB)
ENDIF()

