# Falafel Device

Falafel Device is a learning-oriented embedded project for two ESP32-S3 devices.
The first milestone is deliberately small: run the same LVGL GUI entry point in
a 320 x 480 SDL2 window on macOS.

## Milestone 1: desktop simulator

Requirements currently verified on the development machine:

- macOS on Apple Silicon
- CMake 4.4.4
- Ninja 1.13.2
- Apple Clang 21
- SDL2 2.32.74 (`sdl2-compat` from Homebrew)

Configure, build, and run from the repository root:

```sh
cmake -S simulator -B build/simulator -G Ninja
cmake --build build/simulator
./build/simulator/falafel_simulator
```

The first configure downloads the pinned LVGL 9.4.0 source into the build
directory. No package is installed globally.

The ESP-IDF project under `firmware/` is only a minimal placeholder. Display,
touch, camera, battery, networking, and board pin configuration are intentionally
not implemented yet.

