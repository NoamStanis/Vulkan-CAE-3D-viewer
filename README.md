# Vulkan CAE 3D Viewer

A minimal Qt Quick + Vulkan application that renders a triangle into an
offscreen `VkImage` and composites it into the QML scene graph as a
`QSGTexture`. On macOS, Vulkan runs on top of Metal via **MoltenVK**; on
Windows and Linux it uses the GPU vendor's native Vulkan driver.

Dependencies are managed with **Conan**, configured with **CMake**, and built
with **Ninja**. The C++/QML sources are platform-agnostic — all platform
differences live in `conanfile.py` and the run-time environment.

---

## Prerequisites (all platforms)

- **Python 3** with **Conan 2.x** and **certifi**
  ```bash
  pip install "conan>=2.0" certifi
  ```
- **CMake ≥ 3.21** and **Ninja**

> All other dependencies (Qt 6, the Vulkan loader, shaderc/glslc, glslang, and
> on macOS MoltenVK) are pulled in automatically by Conan — you do **not** need
> the LunarG Vulkan SDK or a system Qt installation.

### Platform-specific toolchain

| Platform | Compiler / extra setup |
| --- | --- |
| **macOS** (tested: Apple Silicon, macOS 26 SDK) | Xcode Command Line Tools (Apple Clang); `brew install cmake ninja` |
| **Windows** | MSVC (Visual Studio Build Tools) — run the build from a *Developer Command Prompt* so `cl.exe` and Ninja are on `PATH`; or install LLVM Clang. CMake + Ninja can be installed via [winget](https://learn.microsoft.com/windows/package-manager/) or the installers. |
| **Linux** | GCC or Clang, plus X11/Wayland and Vulkan driver dev packages; `cmake` + `ninja` from your package manager |

---

## One-time setup

### 1. Let Python trust Conan Center (TLS certificates)

Some Python installs (notably the python.org macOS build) ship without a CA
trust store, which makes Conan fail to reach Conan Center with an SSL error.
If you hit `CERTIFICATE_VERIFY_FAILED`, point Python at certifi's bundle:

```bash
# macOS / Linux
export SSL_CERT_FILE="$(python3 -m certifi)"
```
```powershell
# Windows (PowerShell)
$env:SSL_CERT_FILE = (python -m certifi)
```

On macOS, add the line to `~/.zshrc` to make it permanent (or run
`"/Applications/Python 3.x/Install Certificates.command"` once). Most Windows
and Linux Python installs don't need this at all.

### 2. Create a Conan profile

```bash
conan profile detect --force
```

This auto-detects your compiler and writes `~/.conan2/profiles/default`.

---

## Build

From the project root:

```bash
# 1. Install dependencies and generate CMake integration files.
#    The first run compiles Qt 6 (and, on macOS, MoltenVK) from source —
#    expect this to take a while (potentially 1–3 hours). The cache is reused
#    after that.
conan install . --build=missing -s build_type=Release

# 2. Configure using the Conan-generated CMake preset.
cmake --preset conan-release

# 3. Build the executable with Ninja.
cmake --build build/Release
```

> `cmake --build build/Release` works on every platform. On macOS/Linux you can
> equivalently run `ninja -C build/Release`.

The resulting binary is **`build/Release/VulkanCAEViewerApp`**
(`VulkanCAEViewerApp.exe` on Windows).

---

## Run

The app shares a `QVulkanInstance` with Qt's scene graph and needs a Vulkan
driver discoverable at run time. Conan generates an environment script that
sets up the library/driver paths; source it before launching.

```bash
# macOS (Apple Silicon) — sets VK_ICD_FILENAMES to the bundled MoltenVK driver
source build/Release/generators/conanrunenv-release-armv8.sh   # Intel: ...-x86_64.sh
./build/Release/VulkanCAEViewerApp
```
```bash
# Linux
source build/Release/generators/conanrunenv-release-x86_64.sh
./build/Release/VulkanCAEViewerApp
```
```bat
:: Windows (cmd) — uses the installed GPU vendor's native Vulkan driver
build\Release\generators\conanrun.bat
build\Release\VulkanCAEViewerApp.exe
```

You should see a 1280×720 window titled **"Vulkan CAE Viewer — Step 1"** showing
an RGB-gradient triangle, with a QML overlay bar across the top.

> **macOS:** if the app exits immediately with a Vulkan instance creation
> error, the ICD env var (`VK_ICD_FILENAMES` / `VK_DRIVER_FILES`) was not set —
> make sure you sourced the `conanrunenv-*.sh` script above.
>
> **Windows/Linux:** Vulkan instance creation failing usually means no Vulkan
> driver is installed — install your GPU vendor's current graphics drivers
> (which register the Vulkan ICD system-wide).

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

`conanfile.py` selects the Vulkan/SPIR-V stack per OS:

- **macOS** pins the **1.3.239.0** line (`vulkan-headers`, `vulkan-loader`,
  `shaderc/2023.6`). Qt 6.8.3 depends on `moltenvk/1.2.2`, which only compiles
  against `glslang/1.3.239.0`; keeping the whole stack on that line avoids a
  glslang version conflict. Bumping any of them in isolation breaks the
  MoltenVK build.
- **Windows / Linux** use a current aligned stack (`1.3.290.0` +
  `shaderc/2024.1`). There's no MoltenVK in the graph on these platforms, so
  the glslang conflict doesn't apply.

Qt's PostgreSQL/MySQL/ODBC SQL drivers are disabled on all platforms (not
needed here). On **macOS only**, Qt's OpenGL support is also disabled, because
it pulls in the `AGL` framework which was removed from the macOS 26 SDK; the
app renders via Vulkan/Metal regardless.
