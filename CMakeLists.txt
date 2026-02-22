cmake_minimum_required(VERSION 3.16)
project(imap_copy LANGUAGES CXX)

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
        src/client.cpp
        src/cache.cpp
        src/transfer.cpp
        include/types.h
        include/config.h
        include/client.h
        include/cache.h
        include/transfer.h
)

set_target_properties(imap_copy PROPERTIES OUTPUT_NAME "imap-copy")
target_include_directories(imap_copy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(imap_copy PRIVATE CURL::libcurl Threads::Threads)

# Release-only optimizations.
target_compile_options(imap_copy PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-pipe>
)

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
    set(CLANG_TIDY_EXTRA_ARGS "")
    if (APPLE)
        execute_process(
            COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE APPLE_SDKROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if (APPLE_SDKROOT)
            list(APPEND CLANG_TIDY_EXTRA_ARGS
                -extra-arg=-isysroot
                -extra-arg=${APPLE_SDKROOT}
            )
        endif ()
    endif ()

    add_custom_target(clang-tidy
            COMMAND ${CLANG_TIDY_EXE}
            -p ${CMAKE_BINARY_DIR}
            --quiet
            ${CLANG_TIDY_EXTRA_ARGS}
            --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
            ${CMAKE_SOURCE_DIR}/src/main.cpp
            ${CMAKE_SOURCE_DIR}/src/config.cpp
            ${CMAKE_SOURCE_DIR}/src/client.cpp
            ${CMAKE_SOURCE_DIR}/src/transfer.cpp
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running clang-tidy"
            VERBATIM
    )
else ()
    message(STATUS "clang-tidy not found; 'clang-tidy' target will be unavailable")
endif ()
