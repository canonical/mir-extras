# `mir-foreign-buffer-viewer`
A test client for `ext-foreign-buffer-v1`.

## Build

```sh
cd ext_foreign_buffer_client
cmake -B build
cmake --build build
```

## Running

Press Ctrl+C in the launching terminal, or close the viewer window, to exit.

```sh
cd build
WAYLAND_DISPLAY=wayland-99 ./mir-foreign-buffer-viewer --help
Usage: ./mir-foreign-buffer-viewer [OPTIONS]

A test client for ext_foreign_buffer_v1. Tiles every capture source into one window.

  --toplevels        capture every toplevel (default)
  --outputs          capture every output instead
  --output NAME      in --outputs mode, capture only the named output
  --size WxH         initial window size (default 1280x720)
  --include-self     do not skip this viewer's own toplevel
                     (deliberately triggers the self-capture feedback loop)
  --help             show this message
```
