project(Coroutines_LIB CXX C)

set(LIB_TYPE "STATIC" CACHE STRING "STATIC, SHARED or MODULE?")
message("[Coroutines] \t LIB_TYPE " ${LIB_TYPE})
add_library(Coroutines_LIB ${PROJECT_CPP_FILES} ${PROJECT_INC_FILES})

message("[Coroutines] \t -> CMAKE_INSTALL_PREFIX " ${CMAKE_INSTALL_PREFIX})
install(TARGETS Coroutines_LIB DESTINATION bin)
install(FILES ${PROJECT_INC_FILES} DESTINATION include)

