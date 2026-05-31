# Vulkan CAE 3D Viewer

A minimal Qt Quick + Vulkan application that renders a triangle into an
offscreen `VkImage` and composites it into the QML scene graph as a
`QSGTexture`. On macOS, Vulkan runs on top of Metal via **MoltenVK**.

Dependencies are managed with **Conan**, configured with **CMake**, and built
with **Ninja**.

---

## Prerequisites

- **macOS** (tested on Apple Silicon / arm64, macOS 26 SDK)
- **Xcode Command Line Tools** — provides the Apple Clang compiler
- **Python 3** with **Conan 2.x** and **certifi**
  ```bash
  pip install "conan>=2.0" certifi
  ```
- **CMake ≥ 3.21** and **Ninja**
  ```bash
  brew install cmake ninja
  ```

> All other dependencies (Qt 6, the Vulkan loader, MoltenVK, shaderc/glslc,
> glslang) are pulled in automatically by Conan — you do **not** need the
> LunarG Vulkan SDK or a system Qt installation.

---

## One-time setup

### 1. Let Python trust Conan Center (TLS certificates)

The python.org build of Python ships without a CA trust store, which makes
Conan fail to reach Conan Center with an SSL error. Point Python at certifi's
bundle:

```bash
export SSL_CERT_FILE="$(python3 -m certifi)"
```

Add that line to your `~/.zshrc` to make it permanent. (If you installed Python
from python.org, running `"/Applications/Python 3.x/Install Certificates.command"`
once achieves the same thing.)

### 2. Create a Conan profile

```bash
conan profile detect --force
```

This auto-detects Apple Clang and writes `~/.conan2/profiles/default`.

---

## Build

From the project root:

```bash
# 1. Install dependencies and generate CMake integration files.
#    The first run compiles Qt 6 and MoltenVK from source — expect this to
#    take a while (potentially 1–3 hours). Subsequent runs use the cache.
conan install . --build=missing -s build_type=Release

# 2. Configure using the Conan-generated CMake preset.
cmake --preset conan-release

# 3. Build the executable with Ninja.
ninja -C build/Release
```

The resulting binary is at **`build/Release/VulkanCAEViewerApp`**.

---

## Run

The executable needs the MoltenVK Vulkan driver (ICD) to be discoverable at
runtime. Conan generates an environment script that sets this up:

```bash
# Apple Silicon (arm64):
source build/Release/generators/conanrunenv-release-armv8.sh
# Intel (x86_64) would be: conanrunenv-release-x86_64.sh

./build/Release/VulkanCAEViewerApp
```

You should see a 1280×720 window titled **"Vulkan CAE Viewer — Step 1"** showing
an RGB-gradient triangle, with a QML overlay bar across the top.

> If the app exits immediately with a Vulkan instance creation error, the ICD
> env var (`VK_ICD_FILENAMES` / `VK_DRIVER_FILES`) was not set — make sure you
> sourced the `conanrunenv-*.sh` script above.

---

## Project layout

| Path                     | Purpose                                                        |
| ------------------------ | ------------------------------------------------------------- |
| `conanfile.py`           | Conan recipe: dependencies, Qt options, CMake toolchain setup |
| `CMakeLists.txt`         | Build definition; compiles shaders and the QML module         |
| `src/main.cpp`           | App entry point; forces the Vulkan RHI backend                |
| `src/ViewerItem.*`       | `QQuickItem` that drives the renderer and wraps its output     |
| `src/VulkanRenderer.*`   | Owns the Vulkan objects; renders the triangle offscreen       |
| `shaders/`               | GLSL sources, compiled to SPIR-V via `glslc` at build time    |
| `main.qml`               | UI: the viewport plus overlay                                 |

---

## Notes on dependency versions

The Vulkan/SPIR-V stack is intentionally pinned to the **1.3.239.0** line in
`conanfile.py`. Qt 6.8.3 depends on `moltenvk/1.2.2`, which only compiles
against `glslang/1.3.239.0`; keeping `vulkan-headers`, `vulkan-loader`, and
`shaderc/2023.6` on that same line avoids a glslang version conflict. Bumping
any of them in isolation will break the MoltenVK build.

Qt's PostgreSQL/MySQL/ODBC SQL drivers and OpenGL support are disabled in the
Conan recipe — they aren't needed here, and OpenGL pulls in the `AGL`
framework, which was removed from the macOS 26 SDK.
