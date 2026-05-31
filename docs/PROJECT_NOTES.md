# Project Notes — Vulkan CAE 3D Viewer

Running record of design decisions, rationale, and roadmap, so future sessions
have context without re-deriving it. Newest status at the bottom of each section.

Last updated: 2026-05-31.

---

## 1. What this project is

A Qt Quick + Vulkan viewer aimed at **CAE / vibroacoustics** models — *not* a
generic 3D-graphics demo. The end goal is to view finite-element models and
their simulation results (mode shapes, frequency-response fields), starting from
Nastran. The graphics stack was built up in small, individually-verified steps;
the CAE data layer is now being added on top.

### Build/runtime stack
- **Conan 2.x** for dependencies, **CMake**, **Ninja**. Sources are
  platform-agnostic; platform differences live in `conanfile.py`.
- **Qt 6.8.3** (Conan) provides Qt Quick + the Vulkan RHI backend.
- Vulkan via **MoltenVK** on macOS (Metal), native ICD on Windows/Linux.
- **tinyobjloader** (Conan) for OBJ.
- **VTK 9.6** (Homebrew, *non-Conan*) as a headless data/filter layer for the
  CAE work — see §4.

---

## 2. Architecture (how rendering works)

The viewer renders **off-screen** and composites the result into QML:

1. `VulkanRenderer` (render thread) draws the mesh + axis gizmo into an offscreen
   `VkImage` (color + depth).
2. The image is wrapped as a `QSGTexture` via
   `QNativeInterface::QSGVulkanTexture::fromNative` and placed in a
   `QSGSimpleTextureNode`.
3. `ViewerItem` (a `QQuickItem`) drives this in `updatePaintNode`.

**Why offscreen-texture rather than an embedded native window:** it composites
correctly with the rest of the QML scene (anchoring, clipping, layouts, overlays,
opacity). A native-window approach would break all of those. This is also what
makes the item embeddable as a QML component (see §6).

**Threading:** Vulkan resource create/destroy happens on Qt's render thread
(via `sceneGraphInitialized`/`Invalidated`, `DirectConnection`). The camera lives
on the **main thread**; only the resulting MVP matrix crosses to the render
thread, inside `updatePaintNode` where the main thread is blocked — so no locks.
Resize and input use the same hand-off pattern.

### Key prerequisite for embedding
The whole window must opt into the Vulkan RHI backend
(`QQuickWindow::setGraphicsApi(Vulkan)` + a shared `QVulkanInstance`) **before**
any `QQuickWindow` is created — done in `main.cpp`. A host app embedding
`ViewerItem` must do the same; the component can't configure it itself. This is
an integration contract, not a bug.

---

## 3. How the graphics layer was built (done, verified)

Each step was built and confirmed to build/run before the next.

1. **Vulkan → QSGTexture, threading model.** Offscreen image composited into QML.
2. **Vertex/index buffers + vertex input layout.** Replaced an in-shader
   hardcoded triangle with real buffers (hardcoded cube). Added a depth buffer.
3. **MVP via uniform buffer + descriptor set.** Perspective projection; the cube
   reads as 3D. Back-face culling (CCW front face).
4. **Trackball camera + input.** Quaternion orbit camera; drag-to-orbit,
   scroll-to-zoom wired from a QML `MouseArea` (`onWheel` handles zoom — a
   separate `WheelHandler` did **not** receive events and was removed).
5. **OBJ loading (tinyobjloader) + XYZ axis gizmo.** Camera frames to the loaded
   mesh's bounds. Axes are a second line-topology pipeline sharing the MVP
   descriptor, depth-tested so they occlude correctly.

### Known deferred graphics simplifications
- **Single `m_colorImage` + per-frame blocking `vkWaitForFences`** on the render
  thread (no double-buffering / no semaphore hand-off to Qt). Fine for one
  viewport; revisit (double-buffering) if flicker/tearing appears or for multiple
  instances.
- **Continuous repaint:** `updatePaintNode` calls `update()` every frame, driving
  the window at full framerate even when idle. Switch to on-demand repaint for an
  embedded component (battery/perf).
- **Mesh normals on a no-normal OBJ** are smooth/shared rather than flat per-face
  (de-dup keys on `(position, normal_index)`); acceptable for a viewer.

---

## 4. CAE / vibroacoustics direction & the VTK decision

**Goal (confirmed):** view **mesh + results** (mode shapes, frequency-response
fields), not geometry only. The value in vibroacoustics is the results.

**First format (confirmed):** **Nastran `.bdf`** — the dominant format in
structural dynamics / vibroacoustics.

### Format landscape (for reference)
- FE solver decks: **Nastran `.bdf`/`.dat`/`.nas`** (primary), Abaqus `.inp`,
  ANSYS `.cdb`, LS-DYNA `.k`, OptiStruct/PERMAS `.fem`, **`.unv`** (test/measured
  mode shapes — relevant for experimental modal analysis).
- Acoustics tools: Actran, VA One / wave6, Simcenter/LMS.
- Neutral/interchange: CGNS, Exodus II, MED, **VTK/VTU** (de-facto results-viz).
- CAD geometry (upstream, needs tessellation): STEP, IGES; STL (render-only).

### Why VTK, and how it's used
We chose to use **VTK as a non-Conan dependency**, **headlessly**, as a
data/filter layer only — readers (VTU/results), free-surface extraction,
colormaps/scalar bars — feeding the **existing Vulkan renderer**. VTK's own
OpenGL renderer / actors / render windows are **never** instantiated (they can't
composite into the `QSGTexture` node and would pull in a competing GL/Qt stack).

Decision history (important — the obvious paths were ruled out):
- **VTK is not on Conan Center** (0 recipes). So it can't be a normal Conan dep.
- **Core VTK does not parse Nastran `.bdf`** anyway — so for the chosen first
  format VTK buys us *nothing on parsing*; we hand-write that.
- Therefore: hand-write the Nastran reader; use VTK for the parts it's genuinely
  good at (surface extraction, colormaps, later VTU). Accept a heavy non-Conan
  build (Homebrew bottle) to get that machinery for the results work.

### VTK integration facts
- `brew install vtk` → **9.6.2 bottle** (no source build). It also pulls in its
  own Homebrew Qt 6.11.1 as a dependency — **we do not link VTK's Qt**.
- CMake: `find_package(VTK 9.6 COMPONENTS CommonCore CommonDataModel FiltersCore
  FiltersGeometry)` + `vtk_module_autoinit`, with
  `-DVTK_DIR="$(brew --prefix vtk)/lib/cmake/vtk-9.6"`.
- **Smoke test (passed):** a headless
  `vtkDataSetSurfaceFilter → vtkTriangleFilter → vtkPolyDataNormals` pipeline
  builds/links/runs under Apple Clang 17 and produces a correct surface with
  normals. `otool -L` confirms **no** Rendering/OpenGL/Qt VTK modules are linked
  — the "headless data layer only" architecture holds in practice. Lives in
  `experiments/vtk_smoketest/`.

---

## 5. Nastran reader (Layer 1 — done, verified in isolation)

`src/io/NastranReader.{h,cpp}` parses a `.bdf` subset → `vtkUnstructuredGrid`.

**Handles:** small-field (8-col fixed), large-field (`*`, 16-col, with
`*`-prefixed continuation lines), and free-field (comma) formats; comments and
`BEGIN BULK`/`ENDDATA`; Fortran implicit exponents (`1.0+5` → `1.0e5`); `GRID`
nodes (arbitrary IDs → dense VTK point indices); `CTETRA`/`CHEXA`/`CPENTA`
volumes; `CTRIA3`/`CQUAD4` shells. Returns a `Stats` struct; throws on
unopenable/empty.

**Not yet handled (documented in the header):** results, materials, loads,
`INCLUDE` files, **non-zero coordinate systems** (`CP`/`CORD*` — currently treated
as basic system 0), higher-order elements (`CTETRA10`, etc.).

**Verification:** standalone harness in `experiments/nastran_test/` against
`test/data/{tet_and_shells,formats}.bdf` — all assertions pass (small/large/free
formats, node coordinates), and the parsed grid feeds `vtkDataSetSurfaceFilter`
correctly (Layer-2 hand-off confirmed).

**Not yet wired into the app or the main CMake build** — like the smoke test, it
currently lives only in `experiments/`.

### Roadmap (next)
- **Layer 2:** convert extracted surface `vtkPolyData` → `MeshData`; add VTK to
  the app's CMake; render a real `.bdf` through the existing pipeline. *First
  on-screen FE model.*
- **Layer 3:** mode shapes (deformed geometry per eigenfrequency). Results from
  `.pch`/`.f06` (text, start here) before `.op2` (binary). Prefer GPU vertex-shader
  displacement (enables animation) over CPU re-upload.
- **Layer 4:** scalar-field coloring (SPL/pressure/displacement) via colormap —
  prefer a 1D colormap texture sampled in the fragment shader from a per-vertex
  scalar (interactive range/colormap) over baked vertex colors. Add legend +
  field/frequency selectors in QML. Reuses the axis gizmo's per-vertex-color path.

---

## 6. Embedding as a QML component (assessed, not yet done)

`ViewerItem` is already a registered QML element and composites correctly, so
it's usable within this app today. To make it a *reusable* component for an
arbitrary QML UI, the remaining work is: (a) document/handle the Vulkan-backend
prerequisite (§2), (b) replace the hardcoded asset path with a `source` QML
property (+ async load), (c) on-demand repaint, (d) a small QML property API
(`source`, `showAxes`, `resetCamera()`, `modelBounds`, …). None of this is
architectural rework. Multi-instance support is deferred (single-image renderer,
§3).

---

## 7. Environment gotchas (this machine)

- **Conan Center SSL failures** (`CERTIFICATE_VERIFY_FAILED ... unable to get
  local issuer certificate`) come from the python.org Python 3.13 shipping an
  empty CA trust store — **not** an outage (`curl` to the host works). Fix:
  `"/Applications/Python 3.13/Install Certificates.command"` (installs certifi's
  bundle as the default). Don't assume Conan Center is down when you see this.
- `timeout` is not available in the shell here; use other means to bound commands.

---

## 8. Decision log (quick reference)

| Decision | Choice | Why |
| --- | --- | --- |
| Build system | Conan + CMake + Ninja | Single-command deps; platform-agnostic sources |
| Render strategy | Offscreen `VkImage` → `QSGTexture` | Correct QML compositing; embeddable |
| Mesh loader (graphics) | tinyobjloader via Conan | Header/lib, no dep-graph risk |
| Input handling | QML `MouseArea` → `Q_INVOKABLE` | Idiomatic; one handler owns drag+zoom |
| CAE: scope | Mesh **and** results | Vibroacoustics value is in the results |
| CAE: first format | Nastran `.bdf` | Dominant in the field |
| CAE: library | **VTK, non-Conan, headless** | Not on Conan Center; core VTK can't parse `.bdf`; use it for surface extraction/colormaps/VTU only |
| Nastran parsing | Hand-written | VTK won't do it; keeps control + lean parse |
