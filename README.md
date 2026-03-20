# Black Hole CPU Renderer

A simple CPU-based black hole simulation that ray-traces photon paths and outputs a `.ppm` image.

---

## Requirements

* C++ compiler (GCC, Clang, or MSVC)

---

## Linux

### 1. Compile

```bash
g++ -O3 -fopenmp main.cpp -o bh
```

### 2. Run

```bash
./bh
```

### 3. View Output

```bash
xdg-open blackhole.ppm
```

---

## Windows

### Option A: MinGW / MSYS2

#### 1. Compile

```bash
g++ -O3 -fopenmp main.cpp -o bh
```

#### 2. Run

```bash
bh.exe
```

---

### Option B: Visual Studio (Developer Command Prompt)

#### 1. Compile

```bat
cl /O2 main.cpp
```

#### 2. Run

```bat
main.exe
```

---

## Output

The program generates:

```
blackhole.ppm
```

This is a plain-text image format.

---

## Viewing the Image

### Linux

* `xdg-open blackhole.ppm`
* or open with any image viewer

### Windows

* Open with:

  * IrfanView
  * GIMP
  * Paint.NET

---

## Notes

* Increase resolution by editing `WIDTH` and `HEIGHT` in `main.cpp`
* Higher resolution will increase render time
* This version is CPU-only (no GPU acceleration)

---

## Next Steps

* Improve accuracy (RK4 integration)
* Add textures or starfields
* Port to GPU (Vulkan compute)

---