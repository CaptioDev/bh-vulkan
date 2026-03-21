# Black Hole Vulkan Renderer

A real-time black hole simulation that ray-traces photon paths directly to the screen using Vulkan and GLFW.

---

## Requirements

* C++17 compiler (GCC, Clang, or MSVC)
* [Vulkan SDK](https://vulkan.lunarg.com/) (provides Vulkan headers and `glslangValidator` for shader compilation)
* [GLFW 3](https://www.glfw.org/)
* `pkg-config` (for resolving dependencies on Linux)

---

## Linux Build and Run

### 1. Build Compilation

```bash
make
```

This will automatically compile the shaders (`shader.vert` and `shader.frag` to `.spv` format) and build the `bh_vulkan` executable using `g++`.

### 2. Execution

```bash
./bh_vulkan
```

---

## Controls

* **Left Click + Drag**: Rotate camera around the black hole (adjust yaw and pitch)
* **Scroll Wheel**: Zoom in and out (adjust camera radius)

---

## Notes & Performance Tuning

* The application implements a multi-threaded thread pool design for distributing command buffer recording across CPU cores.
* You can adjust the starting `WIDTH` and `HEIGHT` constants in `main.cpp` to change the rendering resolution.
* This project has been upgraded from an initial CPU-only offline renderer (which generated `.ppm` files) to a real-time hardware-accelerated Vulkan renderer. The gravity lensing and physics have been ported and optimized as a per-pixel raymarching process within the fragment shader (`shader.frag`).
* The window title will dynamically update and display the current Frames Per Second (FPS).