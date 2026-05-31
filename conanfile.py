from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout
import os


class VulkanCAEViewerConan(ConanFile):
    name = "vulkan-cae-viewer"
    version = "0.1"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("qt/6.8.3")
        if self.settings.os == "Macos":
            # On macOS, Qt 6.8.3 hard-pins moltenvk/1.2.2 (the Vulkan-on-Metal
            # driver), which only compiles against glslang/1.3.239.0. Keep the
            # entire Vulkan/SPIR-V stack on that same 1.3.239.0 line so glslang
            # stays consistent across moltenvk and shaderc. shaderc/2023.6 is
            # the version whose glslang dependency matches exactly (newer
            # shaderc pulls glslang 1.3.261+/1.4.x and conflicts). This is what
            # lets MoltenVK build.
            self.requires("vulkan-headers/1.3.239.0")
            self.requires("vulkan-loader/1.3.239.0")
            self.requires("shaderc/2023.6")  # provides the glslc binary
        else:
            # Windows/Linux use the GPU vendor's native Vulkan driver — there's
            # no MoltenVK in the graph, so the glslang conflict above does not
            # apply and we can track a current, aligned Vulkan/SPIR-V stack.
            self.requires("vulkan-headers/1.3.290.0")
            self.requires("vulkan-loader/1.3.290.0")
            self.requires("shaderc/2024.1")  # provides the glslc binary

    def configure(self):
        self.options["qt"].shared = True
        self.options["qt"].qtdeclarative = True   # QtQuick
        self.options["qt"].qtshadertools = True   # QtQuick's Vulkan/Metal shader pipeline
        self.options["qt"].with_vulkan = True
        self.options["qt"].with_pq = False        # PostgreSQL driver — not needed, avoids libpq build error
        self.options["qt"].with_mysql_client = False
        self.options["qt"].with_odbc = False
        if self.settings.os == "Macos":
            # AGL framework was removed from the macOS 26 SDK; on macOS we render
            # via Vulkan/Metal so OpenGL is unnecessary. Leave Qt's default
            # OpenGL support on other platforms.
            self.options["qt"].opengl = "no"

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

        # Force the Ninja generator so `cmake --preset` produces a build.ninja
        # (otherwise Conan defaults to "Unix Makefiles" from the profile).
        tc = CMakeToolchain(self, generator="Ninja")

        # Prefer Conan-generated config files over CMake's built-in module
        # finders (e.g. FindVulkan.cmake), so the remapping above takes effect.
        tc.variables["CMAKE_FIND_PACKAGE_PREFER_CONFIG"] = True

        # Pre-fill the GLSLC cache variable so find_program() in CMakeLists.txt
        # resolves to the glslc binary installed by the shaderc Conan package.
        shaderc_bindirs = self.dependencies["shaderc"].cpp_info.bindirs
        if shaderc_bindirs:
            exe = "glslc.exe" if self.settings.os == "Windows" else "glslc"
            tc.variables["GLSLC"] = os.path.join(shaderc_bindirs[0], exe)

        tc.generate()
