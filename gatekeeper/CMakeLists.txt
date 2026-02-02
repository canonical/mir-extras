# Gatekeeper is a Python-based application

# Copy Python files to build directory
file(COPY 
    ${CMAKE_CURRENT_SOURCE_DIR}/gatekeeper.py
    ${CMAKE_CURRENT_SOURCE_DIR}/test_client.py
    ${CMAKE_CURRENT_SOURCE_DIR}/generate_protocols.py
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

# Check if pywayland is available
find_package(Python3 COMPONENTS Interpreter)

if(Python3_FOUND)
    # Check if pywayland is installed
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import pywayland.scanner"
        RESULT_VARIABLE PYWAYLAND_CHECK
        OUTPUT_QUIET
        ERROR_QUIET
    )
    
    if(PYWAYLAND_CHECK EQUAL 0)
        set(PROTOCOL_DIR ${CMAKE_SOURCE_DIR}/wayland-protocols)
        set(PROTOCOLS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
        
        # Generate the protocol bindings using the generate_protocols.py script
        add_custom_command(
            OUTPUT ${PROTOCOLS_OUTPUT_DIR}/__init__.py
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/generate_protocols.py ${PROTOCOL_DIR}
            DEPENDS 
                ${CMAKE_CURRENT_SOURCE_DIR}/generate_protocols.py
                ${PROTOCOL_DIR}/ext-input-trigger-action-v1.xml
                ${PROTOCOL_DIR}/ext-input-trigger-registration-v1.xml
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating Python protocol bindings for gatekeeper"
        )
        
        # Create a target that depends on the generated files
        add_custom_target(gatekeeper_protocols ALL
            DEPENDS ${PROTOCOLS_OUTPUT_DIR}/__init__.py
        )
    else()
        message(WARNING "pywayland not found, skipping gatekeeper protocol generation")
    endif()
else()
    message(WARNING "Python3 not found, skipping gatekeeper protocol generation")
endif()
