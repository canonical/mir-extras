# mir-extras

This contains code and components that supplement the Mir display server library.

## Components

This project is organized as a CMake-based multi-component repository with the following subprojects:

### trigger_client

A standalone Wayland client that demonstrates the input trigger protocol. It registers keyboard shortcuts (Ctrl+Shift+C, Alt+X, and Alt+Z) and responds to trigger events.

**Requirements:**
- wayland-client
- xkbcommon

### gatekeeper

A Python-based GTK4 application that implements a D-Bus portal for managing global shortcuts. It provides a UI for users to approve and manage keyboard shortcuts requested by applications.

**Requirements:**
- Python 3
- PyGObject (gi)
- GTK4
- pywayland

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Installing

```bash
sudo make install
```

This will install:
- `trigger_client` executable to `/usr/local/bin`
- `gatekeeper.py`, `test_client.py`, and `generate_protocols.py` to `/usr/local/bin`

## Running

### trigger_client

The trigger_client requires a Wayland compositor that implements the ext-input-trigger protocol:

```bash
trigger_client
```

### gatekeeper

Before running gatekeeper, you need to generate the Python protocol bindings:

```bash
cd gatekeeper
python3 generate_protocols.py
```

Then run the gatekeeper:

```bash
./gatekeeper.py
```

To show the shortcuts UI:

```bash
./gatekeeper.py --show-shortcuts
```

To test the gatekeeper with a test client:

```bash
./test_client.py
```
