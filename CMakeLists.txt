cmake_minimum_required(VERSION 3.16)

project(mir-extras
    VERSION 0.1.0
    DESCRIPTION "Code and components that supplement the Mir display server library"
    LANGUAGES CXX C
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add subprojects
add_subdirectory(trigger_client)
add_subdirectory(gatekeeper)
