# gatekeeper

A Python-based GTK4 application that implements a D-Bus portal for managing global keyboard shortcuts.

## Description

Gatekeeper acts as a privileged Wayland client that:
1. Receives shortcut registration requests from applications via D-Bus
2. Presents a UI for users to approve/modify shortcuts
3. Registers the approved shortcuts with the Wayland compositor
4. Notifies applications when shortcuts are triggered

It implements the `org.freedesktop.impl.portal.GlobalShortcuts` D-Bus interface.

## Files

- **gatekeeper.py** - Main application with GTK4 UI and D-Bus portal implementation
- **test_client.py** - Test client that requests shortcuts via D-Bus
- **generate_protocols.py** - Script to generate Python bindings for Wayland protocols

## Dependencies

- Python 3
- PyGObject (gi) with GTK4 bindings
- pywayland
- Wayland compositor with ext-input-trigger protocol support

## Setup

Before running gatekeeper, generate the Python protocol bindings:

```bash
python3 generate_protocols.py
```

This will create a `protocols/` directory with the generated bindings.

## Running

Start the gatekeeper daemon:

```bash
./gatekeeper.py
```

Show the shortcuts management UI:

```bash
./gatekeeper.py --show-shortcuts
```

## Testing

Run the test client to request shortcuts:

```bash
./test_client.py
```

The test client will:
1. Create a session with gatekeeper
2. Request to bind several shortcuts
3. Wait for the user to approve them in the gatekeeper UI
4. Listen for activation events when shortcuts are pressed

## D-Bus Interface

Implements `org.freedesktop.impl.portal.GlobalShortcuts` with methods:
- `CreateSession` - Create a new shortcut session
- `BindShortcuts` - Request to bind keyboard shortcuts
- `ListShortcuts` - List registered shortcuts

And signal:
- `ShortcutsChanged` - Emitted when shortcuts are modified
