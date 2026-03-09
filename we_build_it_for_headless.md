# Building TIC-80 for Headless Linux (Debian 13)

This guide documents how to build TIC-80 fantasy computer for a headless system (no monitor, no GPU) using Xvfb for virtual display.

## System Info
- OS: Debian GNU/Linux 13 (Trixie)
- Architecture: x86_64

## Install Dependencies

First, update and install the required build tools and libraries:

```bash
sudo apt update && sudo apt -y install build-essential cmake git ruby-dev libcurl4-openssl-dev libsdl2-dev xvfb
```

**What each package provides:**
- `build-essential` - GCC compiler, make, etc.
- `cmake` - Build system generator
- `git` - Version control (for cloning)
- `ruby-dev` - Ruby development headers (needed for some TIC-80 features)
- `libcurl4-openssl-dev` - HTTP library (for network features)
- `libsdl2-dev` - Simple DirectMedia Layer (graphics/input abstraction)
- `xvfb` - Virtual framebuffer (run GUI apps without a display)

## Clone the Repository

```bash
git clone --recursive https://github.com/nesbox/TIC-80
cd TIC-80
```

**Note:** The `--recursive` flag is REQUIRED to fetch all submodules (Lua, Python, JavaScript, etc.)

## Configure the Build

Navigate to the build directory and configure with CMake:

```bash
cd build
cmake -DBUILD_WITH_ALL=On ..
```

**Key CMake options used:**
- `-DBUILD_WITH_ALL=On` - Includes all supported scripting languages (Lua, Moonscript, JavaScript, Ruby, Wren, Fennel, Squirrel, Janet, Python, Scheme, WASM)
- **NOT** using `-DBUILD_SDLGPU=On` - This would add OpenGL/GLES dependencies which we don't need for software rendering
- No wayland packages needed - SDL2 will use X11 instead

The configure output shows:
```
BUILD_SDLGPU: OFF
BUILD_WITH_ALL: On
BUILD_WITH_LUA: ON
BUILD_WITH_MOON: ON
BUILD_WITH_JS: ON
BUILD_WITH_RUBY: ON
...
```

## Build

```bash
cmake --build . --parallel
```

Use `--parallel` to use all available CPU cores for faster compilation. This will take several minutes on first build.

## Run Headless

Since there's no physical display, use Xvfb (X Virtual Framebuffer):

```bash
xvfb-run --auto-servernum ./bin/tic80 --skip
```

**Useful options:**
- `--skip` - Skip the startup animation (recommended for headless)
- `--soft` - Force software rendering
- `--cli` - Console-only output

### Example output:
```
 TIC-80 tiny computer
 version 1.2.4-dev (bb3ffe6)
 https://tic80.com (C) 2017-2026

 hello! type help for help
>
```

**Note:** ALSA warnings about "cannot find card '0'" are expected - there's no audio hardware, but TIC-80 will still work.

## Troubleshooting

### No audio
This is expected on a headless system with no sound hardware. The ALSA warnings are harmless.

### "xvfb-run: command not found"
Install xvfb:
```bash
sudo apt install xvfb
```

### Build fails with missing headers
Make sure all -dev packages are installed:
```bash
sudo apt install libsdl2-dev libcurl4-openssl-dev ruby-dev
```

## Why No Wayland/GPU?

The build configuration explicitly avoids:
- `-DBUILD_SDLGPU=On` - Would require OpenGL/GLES libraries
- `libwayland-dev` - Wayland compositor libraries
- `libpipewire-0.3-dev` - Audio server for modern Linux

Instead, we rely on:
- SDL2's software renderer
- X11 (via Xvfb for virtual display)
- ALSA/PulseAudio (with dummy fallback)

This is sufficient for running TIC-80 in headless mode for:
- Running TIC-80 cartridges programmatically
- Server-side game rendering
- CI/CD pipelines
- Development on headless systems

## Binary Location

After building, the executable is at:
```
build/bin/tic80
```

You can copy it anywhere or add to PATH:
```bash
export PATH="$PWD/build/bin:$PATH"
```
