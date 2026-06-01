cmake_minimum_required(VERSION 3.21)
project(VulkanCAEViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt requires AUTOMOC for moc/metatype generation (needed by qt_add_qml_module)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
find_package(Vulkan REQUIRED)
find_package(tinyobjloader REQUIRED)

# VTK is an optional, non-Conan dependency used headlessly for CAE formats
# (Nastran .bdf → surface extraction). It is NOT on Conan Center; install it
# separately (macOS: `brew install vtk`) and point CMake at it with
#   -DVTK_DIR="$(brew --prefix vtk)/lib/cmake/vtk-9.6"
# When absent, the app still builds with OBJ-only support.
#
# IMPORTANT: VTK's architecture must match the app (arm64 on Apple Silicon).
# Use the arm64 Homebrew at /opt/homebrew, not the x86_64 one at /usr/local.
find_package(VTK 9.6 QUIET COMPONENTS
    CommonCore
    CommonDataModel
    FiltersCore
    FiltersGeometry
)
if(VTK_FOUND)
    message(STATUS "VTK ${VTK_VERSION} found — Nastran/.bdf support enabled")
else()
    message(STATUS "VTK not found — building with OBJ-only support "
                   "(set VTK_DIR to enable .bdf import)")
endif()

# Locate glslc for shader compilation
find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin REQUIRED)

# ── Shader compilation ────────────────────────────────────────────────────────
set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/mesh.vert
    ${SHADER_SOURCE_DIR}/mesh.frag
    ${SHADER_SOURCE_DIR}/axes.vert
    ${SHADER_SOURCE_DIR}/axes.frag
    ${SHADER_SOURCE_DIR}/edges.vert
    ${SHADER_SOURCE_DIR}/edges.frag
)

set(SHADER_BINARIES "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT  ${SPV_OUTPUT}
        COMMAND ${GLSLC} ${SHADER} -o ${SPV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_BINARIES ${SPV_OUTPUT})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_BINARIES})

# ── Application ───────────────────────────────────────────────────────────────
# Note: the executable target name must differ from the QML module URI below.
# qt_add_qml_module creates a "<URI>" directory in the build tree; if that
# matches the executable's output name the linker fails with EISDIR.
# WIN32 makes this a GUI app on Windows (no console window); it is ignored on
# other platforms.
qt_add_executable(VulkanCAEViewerApp WIN32
    src/main.cpp
    src/ViewerItem.cpp
    src/ViewerItem.h
    src/VulkanRenderer.cpp
    src/VulkanRenderer.h
    src/Mesh.cpp
    src/Mesh.h
    src/TrackballCamera.cpp
    src/TrackballCamera.h
    src/DisplayMode.h
)

# Nastran/.bdf import is compiled in only when VTK is available.
if(VTK_FOUND)
    target_sources(VulkanCAEViewerApp PRIVATE
        src/io/NastranReader.cpp
        src/io/NastranReader.h
        src/io/VtkSurface.cpp
        src/io/VtkSurface.h
    )
    target_compile_definitions(VulkanCAEViewerApp PRIVATE HAVE_VTK=1)
endif()

add_dependencies(VulkanCAEViewerApp Shaders)

qt_add_qml_module(VulkanCAEViewerApp
    URI VulkanCAEViewer
    VERSION 1.0
    QML_FILES main.qml
)

target_include_directories(VulkanCAEViewerApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${Vulkan_INCLUDE_DIRS}
)

# Pass the shader and asset directories to the app so it can load .spv files and
# meshes at runtime.
target_compile_definitions(VulkanCAEViewerApp PRIVATE
    SHADER_DIR="${SHADER_BINARY_DIR}/"
    ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/"
)

target_link_libraries(VulkanCAEViewerApp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Vulkan::Vulkan
    tinyobjloader::tinyobjloader
)

if(VTK_FOUND)
    target_link_libraries(VulkanCAEViewerApp PRIVATE ${VTK_LIBRARIES})
    # VTK 9 module autoinit: required so factory-backed classes register at runtime.
    vtk_module_autoinit(TARGETS VulkanCAEViewerApp MODULES ${VTK_LIBRARIES})
endif()
