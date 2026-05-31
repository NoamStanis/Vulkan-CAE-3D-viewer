from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout
import os


class VulkanCAEViewerConan(ConanFile):
    name = "vulkan-cae-viewer"
    version = "0.1"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("qt/6.7.1")
        self.requires("vulkan-headers/1.3.268.0")
        self.requires("vulkan-loader/1.3.268.0")
        self.requires("shaderc/2024.0")  # provides the glslc binary

    def configure(self):
        # Qt must be shared on macOS (frameworks), and Quick needs qtdeclarative
        self.options["qt"].shared = True
        self.options["qt"].qtdeclarative = True   # QtQuick
        self.options["qt"].qtshadertools = True   # QtQuick's Vulkan/Metal shader pipeline
        self.options["qt"].with_vulkan = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)

        # CMakeLists.txt calls find_package(Vulkan) which expects the target
        # Vulkan::Vulkan.  Remap the Conan vulkan-loader package so CMake sees
        # a "Vulkan" config file with that exact target name.
        deps.set_property("vulkan-loader", "cmake_file_name", "Vulkan")
        deps.set_property("vulkan-loader", "cmake_target_name", "Vulkan::Vulkan")
        deps.generate()

        tc = CMakeToolchain(self)

        # Prefer Conan-generated config files over CMake's built-in module
        # finders (e.g. FindVulkan.cmake), so the remapping above takes effect.
        tc.variables["CMAKE_FIND_PACKAGE_PREFER_CONFIG"] = True

        # Pre-fill the GLSLC cache variable so find_program() in CMakeLists.txt
        # resolves to the glslc binary installed by the shaderc Conan package.
        shaderc_bindirs = self.dependencies["shaderc"].cpp_info.bindirs
        if shaderc_bindirs:
            glslc = os.path.join(shaderc_bindirs[0], "glslc")
            tc.variables["GLSLC"] = glslc

        tc.generate()
