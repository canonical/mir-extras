# mir-extras

This contains code and components that supplement the Mir display server library.

## Components

This project is organized as a CMake-based multi-component repository with the following subprojects:

### trigger_client

A standalone Wayland client that demonstrates the input trigger protocol. It registers keyboard shortcuts (Ctrl+Shift+C, Alt+X, and Alt+Z) and responds to trigger events.

**Requirements:**
- wayland-client
- xkbcommon
- wayland-scanner (build-time)

### gatekeeper

A Python-based GTK4 application that implements a D-Bus portal for managing global shortcuts. It provides a UI for users to approve and manage keyboard shortcuts requested by applications.

**Requirements:**
- Python 3
- PyGObject (gi)
- GTK4
- pywayland

## Building

### Ubuntu/Debian

Install dependencies:

```bash
# For trigger_client
sudo apt install libwayland-dev libxkbcommon-dev wayland-protocols

# For gatekeeper
sudo apt install python3 python3-pywayland python3-gi gir1.2-gtk-4.0
```

Build the project:

```bash
mkdir build
cd build
cmake ..
make
```

### Build Options

You can build specific components:

```bash
cmake -Bbuild -DCOMPONENTS="trigger_client" .  # Build only trigger_client
cmake -Bbuild -DCOMPONENTS="gatekeeper" .      # Build only gatekeeper
cmake -Bbuild -DCOMPONENTS="trigger_client;gatekeeper" .  # Build specific components
```

## Running

### trigger_client

The trigger_client requires a Wayland compositor that implements the ext-input-trigger protocol:

```bash
./build/trigger_client/trigger_client
```

### gatekeeper

The Python protocol bindings are automatically generated during the build process (requires python3-pywayland).

Run the gatekeeper from the build directory:

```bash
cd build/gatekeeper
./gatekeeper.py
```

To show the shortcuts UI:

```bash
cd build/gatekeeper
./gatekeeper.py --show-shortcuts
```

To test the gatekeeper with a test client:

```bash
cd build/gatekeeper
./test_client.py
```

