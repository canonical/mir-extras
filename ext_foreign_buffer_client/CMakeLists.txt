cmake_minimum_required(VERSION 3.16)
project(mir-foreign-buffer-viewer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wayland-protocols")
if(NOT EXISTS "${PROTOCOL_DIR}/ext-foreign-buffer-v1.xml")
    message(FATAL_ERROR
        "ext-foreign-buffer-v1.xml not found under ${PROTOCOL_DIR}. "
        "Pass -DMIR_SOURCE_DIR=/path/to/mir")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(GENERATED_SOURCES "")

# Generates <stem>-client-protocol.h and <stem>-protocol.c from a protocol XML.
function(generate_protocol stem)
    set(xml "${PROTOCOL_DIR}/${stem}.xml")
    set(header "${GENERATED_DIR}/${stem}-client-protocol.h")
    set(source "${GENERATED_DIR}/${stem}-protocol.c")

    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${header}"
        DEPENDS "${xml}"
        VERBATIM)

    add_custom_command(
        OUTPUT "${source}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${source}"
        DEPENDS "${xml}"
        VERBATIM)

    set(GENERATED_SOURCES ${GENERATED_SOURCES} "${header}" "${source}" PARENT_SCOPE)
endfunction()

foreach(protocol
        xdg-shell
        viewporter
        ext-foreign-toplevel-list-v1
        ext-image-capture-source-v1
        ext-foreign-buffer-v1)
    generate_protocol(${protocol})
endforeach()

add_executable(mir-foreign-buffer-viewer
    foreign_buffer_viewer.c
    ${GENERATED_SOURCES})

target_include_directories(mir-foreign-buffer-viewer PRIVATE "${GENERATED_DIR}")
target_link_libraries(mir-foreign-buffer-viewer PRIVATE PkgConfig::WAYLAND_CLIENT m rt)
target_compile_options(mir-foreign-buffer-viewer PRIVATE -Wall -Wextra)
