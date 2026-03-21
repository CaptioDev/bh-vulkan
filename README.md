# Black Hole Vulkan Renderer

![Black Hole Simulation Preview](preview.png)

A real-time GPU-accelerated black hole simulation that ray-traces photon paths through curved spacetime using Vulkan. This renderer simulates gravitational lensing, accretion disk physics, and relativistic effects around a rotating (Kerr) black hole.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [File Structure](#file-structure)
3. [Physics & Mathematics](#physics--mathematics)
   - [Kerr Metric](#kerr-metric)
   - [Gravitational Lensing](#gravitational-lensing)
   - [Accretion Disk Physics](#accretion-disk-physics)
   - [Integration Methods](#integration-methods)
4. [Code Components](#code-components)
   - [main.cpp](#maincpp)
   - [sweep.cpp](#sweepcpp)
   - [Shaders](#shaders)
5. [Build Instructions](#build-instructions)
6. [Controls](#controls)
7. [Optimizations](#optimizations)
8. [Output Formats](#output-formats)
9. [Requirements](#requirements)

---

## Project Overview

This project implements two rendering modes:

1. **Real-time Renderer (`main.cpp` + `shader.frag`)**: Interactive Vulkan application rendering at 60+ FPS with camera controls
2. **Offline Sweep Renderer (`sweep.cpp` + `sweep.comp`)**: Compute shader-based offline renderer that generates statistical data and images for analysis

The simulation models a **Kerr black hole** (rotating black hole) using the Kerr metric from General Relativity, including:
- Gravitational light bending (gravitational lensing)
- Spinning accretion disk with relativistic effects
- Doppler beaming (relativistic beaming)
- ISCO (Innermost Stable Circular Orbit) calculations
- Event horizon and ergosphere visualization

---

## File Structure

| File | Description |
|------|-------------|
| [`main.cpp`](main.cpp:1) | Main real-time Vulkan renderer with window management, camera controls, and render loop |
| [`sweep.cpp`](sweep.cpp:1) | Offline compute shader renderer for generating statistical sweeps |
| [`shader.vert`](shader.vert:1) | Simple vertex shader for full-screen quad rendering |
| [`shader.frag`](shader.frag:1) | Fragment shader with Kerr metric raymarching and accretion disk rendering |
| [`sweep.comp`](sweep.comp:1) | Compute shader version of the renderer for offline analysis |
| [`Makefile`](Makefile:1) | Build system for compiling shaders and binaries |
| [`.gitignore`](.gitignore:1) | Git ignore patterns for build artifacts |

---

## Physics & Mathematics

### Kerr Metric

The simulation uses the **Kerr metric** to describe spacetime around a rotating black hole. Unlike the Schwarzschild metric (non-rotating), the Kerr metric includes the spin parameter `a` (0 ≤ a < 1).

The shader computes the Kerr radial coordinate using:

```math
r^2 = \frac{1}{2}(R^2 - a^2 + \sqrt{(R^2 - a^2)^2 + 4a^2y^2})
```

Where:
- `R` is the Cartesian radius
- `y` is the vertical coordinate  
- `a` is the spin parameter (dimensionless, 0-1)

### Gravitational Lensing

Light rays bend due to spacetime curvature. The photon momentum `p` evolves according to:

```math
\frac{dp}{d\lambda} = -\nabla K
```

Where `K` is an effective potential computed in [`compute_K()`](shader.frag:15):

```math
K = f \cdot (k \cdot p)^2
```

With:
```math
f = \frac{2Mr^2}{r^4 + a^2y^2}
```

The force calculation in [`compute_force()`](shader.frag:33) uses numerical differentiation to compute the gradient.

### Accretion Disk Physics

The accretion disk is modeled with several physical phenomena:

#### Innermost Stable Circular Orbit (ISCO)

The ISCO radius determines the inner edge of the stable accretion disk. Computed in [`compute_isco()`](shader.frag:85):

```math
Z_1 = 1 + (1-a^2)^{1/3}((1+a)^{1/3} + (1-a)^{1/3})
```

```math
Z_2 = \sqrt{3a^2 + Z_1^2}
```

```math
r_{ISCO} = 3 + Z_2 - \sqrt{(3-Z_1)(3+Z_1+2Z_2)}
```

#### Novikov-Thorne Model

The disk temperature profile follows:

```math
T(r) \propto \left(\frac{1 - \sqrt{r_{ISCO}/r}}{r^3}\right)^{1/4}
```

#### Doppler Beaming

Relativistic beaming affects observed intensity based on velocity toward/away from observer:

```math
g = \frac{E_{observed}}{E_{emitted}} = \frac{p_t}{p_t U_t - p_\phi U_\phi}
```

Where `g > 1` means blueshift, `g < 1` means redshift.

#### Bardeen-Petterson Effect

The disk exhibits **disk warping** near the black hole due to frame-dragging, implemented in [`get_disk_height()`](shader.frag:94):

```math
\text{tilt} = 0.5 \cdot \text{smoothstep}(r_{ISCO} + 0.5, r_{ISCO} + 12.0, r)
```

```math
\text{twist} = \frac{15}{r}
```

```math
\text{height} = r \cdot \tan(\text{tilt}) \cdot \sin(\phi - \text{twist})
```

#### Lin-Shu Spiral Density

The disk includes spiral arm structure:

```math
\text{spiral} = 1 + 0.85 \cdot \cos(2\phi - 4\ln(r) - t \cdot 2)
```

### Integration Methods

The simulation uses **Runge-Kutta 4th order (RK4)** integration in [`RK4_step()`](shader.frag:65):

```math
x_{n+1} = x_n + \frac{dt}{6}(k_1 + 2k_2 + 2k_3 + k_4)
```

Where each `k` represents intermediate slope calculations at different points along the trajectory.

---

## Code Components

### main.cpp

The main real-time renderer containing:

| Component | Lines | Description |
|-----------|-------|-------------|
| [`ThreadPool`](main.cpp:24) | 24-67 | Multi-threaded task queue for parallel command buffer recording |
| [`BlackHoleRenderer`](main.cpp:70) | 70-784 | Main application class |
| [`initWindow()`](main.cpp:115) | 115-145 | GLFW window initialization with callbacks |
| [`initVulkan()`](main.cpp:201) | 201-214 | Vulkan subsystem initialization chain |
| [`createSwapChain()`](main.cpp:298) | 298-336 | Swap chain creation for presentation |
| [`createGraphicsPipeline()`](main.cpp:422) | 422-541 | Graphics pipeline with shader modules |
| [`mainLoop()`](main.cpp:543+) | 543+ | Main render loop with sync primitives |
| [`onMouseMoved()`](main.cpp:178) | 178-191 | Camera yaw/pitch control |
| [`onScroll()`](main.cpp:193) | 193-199 | Camera zoom (radius) control |
| [`onKey()`](main.cpp:147) | 147-167 | Spin parameter (a) adjustment |

**Key Variables:**
- `spin_a`: Black hole spin parameter (-0.9999 to 0.9999)
- `cameraYaw`, `cameraPitch`, `cameraRadius`: Camera orientation
- `WIDTH` = 1280, `HEIGHT` = 720: Default resolution
- `MAX_FRAMES_IN_FLIGHT` = 2: Triple buffering with 2 frames

### sweep.cpp

Offline compute shader renderer that generates statistical data:

| Component | Lines | Description |
|-----------|-------|-------------|
| [`RayStats`](sweep.cpp:12) | 12-17 | Output structure (intensity, redshift, winding, caustic) |
| [`main()`](sweep.cpp:35) | 35-296 | Main execution with device setup |
| Sweep Loop | 217-283 | Iterates spin values: 0.0, 0.5, 0.99 |

**Outputs:**
- CSV files: `sweep_stats_a0.csv`, `sweep_stats_a1.csv`, `sweep_stats_a2.csv`
- PPM images: `sweep_image_a0.ppm`, `sweep_image_a1.ppm`, `sweep_image_a2.ppm`

### Shaders

#### shader.vert (Vertex Shader)

Simple full-screen quad using `gl_VertexIndex`:

```glsl
outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
```

#### shader.frag (Fragment Shader)

Real-time renderer with all physics:

| Function | Lines | Purpose |
|----------|-------|---------|
| [`compute_K()`](shader.frag:15) | 15-31 | Kerr metric effective potential |
| [`compute_force()`](shader.frag:33) | 33-45 | Gradient of potential (gravity) |
| [`compute_velocity()`](shader.frag:47) | 47-63 | Photon 4-momentum velocity |
| [`RK4_step()`](shader.frag:65) | 65-83 | 4th-order integration |
| [`compute_isco()`](shader.frag:85) | 85-90 | ISCO radius calculation |
| [`get_disk_height()`](shader.frag:94) | 94-101 | Bardeen-Petterson warping |
| [`evaluate_disk()`](shader.frag:103) | 103-145 | Accretion disk emission |
| [`render()`](shader.frag:147) | 147-234 | Main raymarching loop |
| [`main()`](shader.frag:236) | 236-242 | Entry point with UV scaling |

**Push Constants:**
```glsl
layout(push_constant) uniform PushConstants {
    vec4 params;    // x: time, y: width, z: height, w: mass
    vec4 cameraPos; // xyz: position, w: spin_a
} pc;
```

#### sweep.comp (Compute Shader)

Parallel raytracer outputting to storage buffer:

| Function | Lines | Purpose |
|----------|-------|---------|
| [`compute_K()`](sweep.comp:25) | 25-38 | Same as fragment shader |
| [`compute_force()`](sweep.comp:40) | 40-50 | Same as fragment shader |
| [`compute_velocity()`](sweep.comp:52) | 52-65 | Same as fragment shader |
| [`RK4_step()`](sweep.comp:67) | 67-84 | Same as fragment shader |
| [`compute_isco()`](sweep.comp:86) | 86-91 | Same as fragment shader |
| [`main()`](sweep.comp:93) | 93-182 | Per-pixel dispatch with output |

**Dispatch:** `WIDTH/16 x HEIGHT/16 x 1` workgroups (16x16 = 256 threads)

---

## Build Instructions

### Prerequisites

- **Linux** (tested on Arch Linux)
- C++20 compiler (GCC or Clang)
- [Vulkan SDK](https://vulkan.lunarg.com/) (for headers and `glslangValidator`)
- [GLFW 3](https://www.glfw.org/)
- `pkg-config`

### Build Commands

```bash
# Full build (shaders + binaries)
make

# Build only the real-time renderer
make bh_vulkan

# Build only the sweep renderer
make sweep_bin

# Compile shaders only
make shaders

# Clean build artifacts
make clean
```

### Running

```bash
# Real-time renderer
./bh_vulkan

# Offline sweep renderer (generates CSV/PPM files)
./sweep_bin
```

---

## Controls

### Mouse Controls

| Input | Action |
|-------|--------|
| **Left Click + Drag** | Rotate camera (yaw and pitch) |
| **Scroll Wheel** | Zoom in/out (adjust camera radius) |

### Keyboard Controls

| Key | Action |
|-----|--------|
| **← Arrow** | Decrease black hole spin (a -= 0.01) |
| **→ Arrow** | Increase black hole spin (a += 0.01) |
| **↑ Arrow** | Increase camera pitch |
| **↓ Arrow** | Decrease camera pitch |

### Camera Constraints

- **Pitch**: Limited to ±1.5 radians
- **Radius**: Limited to 2.5 - 100 units
- **Spin (a)**: Limited to ±0.9999

---

## Optimizations

### Implemented Optimizations

1. **Adaptive Step Size**: Ray integration uses `dt = STEP_BASE * max(0.1, r/10.0)` to take larger steps when far from the black hole

2. **Early Termination**: Rays terminate when:
   - Crossing event horizon: `r <= r_H * 1.01`
   - Escaping to infinity: `r > 85.0`
   - Alpha (opacity) drops below threshold: `alpha < 0.005`

3. **Thread Pool**: `main.cpp` includes a `ThreadPool` class for parallel task distribution

4. **Push Constants**: Camera and parameters passed via push constants (faster than uniform buffers)

5. **Full-screen Quad**: Uses vertex shader-generated quad (no geometry buffers)

### Render Parameters

| Parameter | Value | Effect |
|-----------|-------|--------|
| `MAX_STEPS` | 300 | Maximum raymarch iterations |
| `STEP_BASE` | 0.8 | Base integration timestep |
| `MAX_FRAMES_IN_FLIGHT` | 2 | Triple buffering |

---

## Output Formats

### CSV Statistics (sweep.cpp)

```csv
x,y,intensity,redshift,winding_number,is_caustic
0,0,0.123,1.02,0,0
...
```

| Column | Description |
|--------|-------------|
| `x`, `y` | Pixel coordinates |
| `intensity` | Accumulated brightness |
| `redshift` | Final Doppler factor g |
| `winding_number` | Times ray crossed disk plane |
| `is_caustic` | High intensity flag (>3000) |

### PPM Images

RGB false-color visualization:
- **Red**: Intensity × 2.0
- **Green**: Redshift × 100
- **Blue**: Winding number × 128

---

## Requirements

### Software

- **OS**: Linux (may work on Windows/macOS with adjustments)
- **Compiler**: GCC 11+ or Clang 13+
- **Vulkan**: API Version 1.0+
- **CMake/pkg-config**: For dependency resolution

### Hardware

- **GPU**: Any Vulkan-capable GPU
- **Memory**: 4GB+ recommended
- **Display**: 1280×720 minimum resolution

### Dependencies (Arch Linux)

```bash
sudo pacman -S vulkan-devel glfw pkgconf
```

---

## Technical Notes

### Thread Pool Implementation

The `ThreadPool` class (lines 24-67 in main.cpp) uses:
- `std::jthread` for structured thread management
- `std::mutex` and `std::condition_variable` for thread-safe task queue
- Template `enqueue()` method for flexible task submission

### Rendering Pipeline

1. **Vertex Shader**: Generates full-screen triangle strip
2. **Fragment Shader**: Per-pixel raymarching with physics
3. **Swap Chain**: Double/triple buffering with FIFO present mode
4. **Synchronization**: Semaphores for GPU-CPU sync

### Event Horizon Calculation

```glsl
float r_H = M + sqrt(max(0.0, M*M - a*a));
```

For non-rotating (a=0): `r_H = M`
For extremal (a→1): `r_H → 0` (naked singularity prevention in shader)

---

*Documentation generated for bh-vulkan project*
