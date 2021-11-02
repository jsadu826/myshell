cmake_minimum_required(VERSION 2.7)

project(myshell)

set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror -pedantic -Wconversion -Wno-unused-result")
file(GLOB SOURCE_FILES "*.c")

add_executable(myshell ${SOURCE_FILES})
add_executable(myshell_memory_check ${SOURCE_FILES})

target_compile_options(myshell_memory_check PUBLIC -fsanitize=address,leak,undefined -fno-omit-frame-pointer)
target_link_libraries(myshell_memory_check -fsanitize=address,leak,undefined)
