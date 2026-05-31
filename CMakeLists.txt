cmake_minimum_required(VERSION 3.21)
project(VulkanCAEViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt requires AUTOMOC for moc/metatype generation (needed by qt_add_qml_module)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
find_package(Vulkan REQUIRED)

# Locate glslc for shader compilation
find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin REQUIRED)

# ── Shader compilation ────────────────────────────────────────────────────────
set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/triangle.vert
    ${SHADER_SOURCE_DIR}/triangle.frag
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
qt_add_executable(VulkanCAEViewerApp
    src/main.cpp
    src/ViewerItem.cpp
    src/ViewerItem.h
    src/VulkanRenderer.cpp
    src/VulkanRenderer.h
)

add_dependencies(VulkanCAEViewerApp Shaders)

qt_add_qml_module(VulkanCAEViewerApp
    URI VulkanCAEViewer
    VERSION 1.0
    QML_FILES main.qml
)

target_include_directories(VulkanCAEViewerApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
)

# Pass the shader directory to the app so it can load .spv files at runtime
target_compile_definitions(VulkanCAEViewerApp PRIVATE
    SHADER_DIR="${SHADER_BINARY_DIR}/"
)

target_link_libraries(VulkanCAEViewerApp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Vulkan::Vulkan
)
