# Split Client Example

This example demonstrates a Wayland client that registers a global keyboard shortcut and spawns a terminal window with two child clients side-by-side using `tmux`.

## Functionality

1.  **Parent Client**:
    -   Connects to the Wayland server.
    -   Registers a global shortcut: `Ctrl + Alt + A`.
    -   Once the shortcut is registered, it receives a token.
    -   It launches a new terminal window (using `gnome-terminal` by default).
    -   Inside the terminal, it starts a `tmux` session.
    -   The `tmux` session is split horizontally into two panes.
    -   Each pane runs a "child" instance of this same client executable, passing the token and a unique ID (1 and 2).

2.  **Child Clients**:
    -   Connect to the Wayland server.
    -   Use the token provided by the parent to bind to the specific trigger action.
    -   Listen for trigger events.
    -   When `Ctrl + Alt + A` is pressed, both clients receive "begin" and "end" events and print their status to their respective terminal panes.

## Prerequisites

To run this example, you need:

-   A Wayland compositor supporting the `ext-input-trigger-v1` protocol (e.g., Mir).
-   `tmux`: To manage the split terminal view.
-   `gnome-terminal`: The default terminal emulator used to launch the view.
    -   *Note: If `gnome-terminal` is not found, the example attempts to run `tmux` in the current terminal, which might nest inside your current session.*

## Usage

1.  Build the project (ensure `split_client` is included in the build).
2.  Run the executable:
    ```bash
    ./build/split_client/split_client
    ```
3.  A new terminal window should appear with a split view.
4.  Press `Ctrl + Alt + A`.
5.  Observe the output in both panes:
    ```
    Begin 1
    End 1
    ```
    and
    ```
    Begin 2
    End 2
    ```
6.  Close the terminal window or press `Ctrl + C` in the parent terminal to exit.