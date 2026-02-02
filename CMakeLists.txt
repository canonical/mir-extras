cmake_minimum_required(VERSION 3.16)

project(mir-extras
    VERSION 0.1.0
    DESCRIPTION "Code and components that supplement the Mir display server library"
    LANGUAGES CXX C
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define available components
set(AVAILABLE_COMPONENTS trigger_client gatekeeper)

# Option for selecting components to build (defaults to all)
set(COMPONENTS "all" CACHE STRING "Components to build (semicolon-separated list or 'all')")

# Process COMPONENTS option
if(COMPONENTS STREQUAL "all")
    set(COMPONENTS_TO_BUILD ${AVAILABLE_COMPONENTS})
else()
    set(COMPONENTS_TO_BUILD ${COMPONENTS})
endif()

# Add subprojects based on selected components
foreach(COMPONENT ${COMPONENTS_TO_BUILD})
    if(NOT COMPONENT IN_LIST AVAILABLE_COMPONENTS)
        message(WARNING "Unknown component: ${COMPONENT}")
    else()
        add_subdirectory(${COMPONENT})
    endif()
endforeach()
