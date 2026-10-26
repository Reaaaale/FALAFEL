# Desktop simulator

This target hosts LVGL in an SDL2 window matching the physical display's
portrait resolution (320 x 480). It contains platform startup and the event
loop; the actual screen construction lives in `../shared/gui/`.

The Falafel OS illustrations live in `../shared/assets/` and are embedded in
`falafel_assets.c`, so the running GUI does not depend on desktop file paths.
After intentionally replacing an illustration, regenerate that source with:

```sh
python3 tools/embed_gui_assets.py
```

The simulator enables LVGL's built-in LodePNG decoder; no additional runtime
library is required.

From the repository root:

```sh
cmake -S simulator -B build/simulator -G Ninja
cmake --build build/simulator
./build/simulator/falafel_simulator --device 1 --peer 2 --data /tmp/falafel-a.state
```

Press `Control-C` in the terminal, or close the SDL window, to stop it.

Start the relay and second instance as described in the root README. Network I/O
is non-blocking, including DNS and connect, and reconnects automatically. The
device state file retains the outgoing queue, received inbox, ID reservation and
synchronized personality across restarts. Its explicit version-2 format uses
fixed-width fields and CRC-32; corrupt or unsupported files stop startup.

The status bar's `R` dot reports only relay connectivity. Peer presence is not
implemented.

On the home screen, personalities can be changed either by dragging at least
50 pixels horizontally with the left mouse button, or by scrolling horizontally
with two fingers on a macOS trackpad. A vertical gesture is ignored. Trackpad
input is observed with `SDL_AddEventWatch`, so LVGL remains the sole owner of
the SDL event polling loop.

To inspect the horizontal wheel values reported by the installed SDL2 or
sdl2-compat runtime, enable the focused input log:

```sh
FALAFEL_INPUT_DEBUG=1 ./build/simulator/falafel_simulator
```

This prints only horizontal `SDL_MOUSEWHEEL` events and recognized swipes. The
normal run logs recognized trackpad swipes but not every wheel event.
