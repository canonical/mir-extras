# Gatekeeper is a Python-based application
# We'll just install the Python scripts

# Install the Python scripts
install(
    PROGRAMS 
        gatekeeper.py
        test_client.py
        generate_protocols.py
    DESTINATION bin
)

# Create a custom target to generate the protocols
# This requires pywayland to be installed
add_custom_target(gatekeeper_protocols
    COMMAND ${CMAKE_COMMAND} -E echo "To generate protocol bindings, run: python3 generate_protocols.py"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Gatekeeper protocol generation (requires pywayland)"
)
