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
sudo apt install python3 python3-gi gir1.2-gtk-4.0
pip3 install pywayland
```

Build the project:

```bash
mkdir build
cd build
cmake ..
make
```

### Build Options

You can disable building specific components:

```bash
cmake -DBUILD_TRIGGER_CLIENT=OFF ..  # Skip trigger_client
cmake -DBUILD_GATEKEEPER=OFF ..      # Skip gatekeeper
```

## Installing

```bash
sudo make install
```

This will install:
- `trigger_client` executable to `/usr/local/bin` (if built)
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

## Project Structure

```
mir-extras/
├── CMakeLists.txt              # Main CMake configuration
├── README.md                   # This file
├── LICENSE                     # Project license
├── .gitignore                  # Git ignore rules
├── wayland-protocols/          # Wayland protocol XML files
│   ├── ext-input-trigger-registration-v1.xml
│   └── ext-input-trigger-action-v1.xml
├── trigger_client/             # C++ Wayland client
│   ├── CMakeLists.txt
│   ├── README.md
│   └── main.cpp
└── gatekeeper/                 # Python D-Bus portal
    ├── CMakeLists.txt
    ├── README.md
    ├── gatekeeper.py
    ├── test_client.py
    └── generate_protocols.py
```
