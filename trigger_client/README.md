# trigger_client

A standalone Wayland client that demonstrates the ext-input-trigger protocol extensions.

## Description

This client registers multiple keyboard shortcuts using both keysym and keycode triggers:
- **Ctrl+Shift+C** - A keysym trigger (character-based, layout-dependent)
- **Alt+X** - A keysym trigger 
- **Alt+Z (scancode 44)** - A keycode trigger (position-based, layout-independent)

When these shortcuts are pressed, the client receives begin/end events and prints messages to stdout.

## Building

This component is built as part of the main mir-extras CMake project:

```bash
mkdir build && cd build
cmake ..
make
```

## Dependencies

- wayland-client
- xkbcommon
- wayland-scanner (build-time)

## Running

The trigger_client requires a Wayland compositor that implements the ext-input-trigger-registration-v1 and ext-input-trigger-action-v1 protocols:

```bash
./trigger_client
```

The client will connect to the Wayland display, register the triggers, and wait for keyboard events. Press Ctrl+C to exit.

## Protocol Extensions

This client uses the following Wayland protocol extensions:
- `ext_input_trigger_registration_v1` - For registering input triggers
- `ext_input_trigger_action_v1` - For receiving action notifications

The protocol XML files are located in `../wayland-protocols/`.
