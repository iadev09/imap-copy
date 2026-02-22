cmake_minimum_required(VERSION 3.16)
project(imap_copy)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(imap_copy
        src/main.cpp
        src/config.cpp
        src/imap_client.cpp
        src/transfer.cpp
)

set_target_properties(imap_copy PROPERTIES OUTPUT_NAME "imap-copy")
target_include_directories(imap_copy PRIVATE include)
target_link_libraries(imap_copy PRIVATE CURL::libcurl Threads::Threads)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(imap_copy PRIVATE -pthread)
    target_link_options(imap_copy PRIVATE -pthread)
    target_compile_definitions(imap_copy PRIVATE
            _GNU_SOURCE
            _POSIX_C_SOURCE=200809L
    )
endif ()

find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if (CLANG_TIDY_EXE)
    add_custom_target(clang-tidy
            COMMAND ${CLANG_TIDY_EXE}
            -p ${CMAKE_BINARY_DIR}
            --quiet
            --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
            ${CMAKE_SOURCE_DIR}/src/main.cpp
            ${CMAKE_SOURCE_DIR}/src/config.cpp
            ${CMAKE_SOURCE_DIR}/src/imap_client.cpp
            ${CMAKE_SOURCE_DIR}/src/transfer.cpp
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy"
            VERBATIM
    )
else ()
    message(STATUS "clang-tidy not found; 'clang-tidy' target will be unavailable")
endif ()
