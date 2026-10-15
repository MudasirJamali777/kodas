cmake_minimum_required(VERSION 3.15)
project(Kodas C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Seed compiler sources
file(GLOB SEED_SOURCES seed/src/*.c)

add_executable(kodas ${SEED_SOURCES})
target_include_directories(kodas PRIVATE seed/src)

# OS-specific flags/libs
if(MSVC)
    # Warning level 3, keep it as warnings (not errors), silence secure-crt deprecations
    target_compile_options(kodas PRIVATE /W3 /wd4996)
elseif(WIN32)
    # MinGW
    target_link_libraries(kodas PRIVATE m)
elseif(APPLE)
    target_link_libraries(kodas PRIVATE m)
else()
    target_link_libraries(kodas PRIVATE m rt)
endif()

# Output binary to seed/ so it can find tests relative to CWD
set_target_properties(kodas PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/seed
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/seed
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/seed
)
