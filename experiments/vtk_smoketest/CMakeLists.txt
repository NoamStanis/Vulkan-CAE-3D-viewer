cmake_minimum_required(VERSION 3.21)
project(VTKSmokeTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headless data/filter components only — no rendering, no Qt. These are the
# pieces the viewer will use to turn an unstructured grid into a renderable
# surface; deliberately excludes VTK's OpenGL/render-window modules.
find_package(VTK 9.6 REQUIRED
    COMPONENTS
        CommonCore
        CommonDataModel
        FiltersCore
        FiltersGeometry
)

add_executable(smoketest smoketest.cpp)
target_link_libraries(smoketest PRIVATE ${VTK_LIBRARIES})

# VTK 9 uses module autoinit; required so factory-backed classes register.
vtk_module_autoinit(TARGETS smoketest MODULES ${VTK_LIBRARIES})
