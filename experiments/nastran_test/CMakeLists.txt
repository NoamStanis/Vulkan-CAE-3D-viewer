cmake_minimum_required(VERSION 3.21)
project(NastranReaderTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(VTK 9.6 REQUIRED
    COMPONENTS
        CommonCore
        CommonDataModel
        FiltersCore
        FiltersGeometry
)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

add_executable(nastran_test
    nastran_test.cpp
    ${SRC}/io/NastranReader.cpp
    ${SRC}/io/VtkSurface.cpp
    ${SRC}/Mesh.cpp
)
target_include_directories(nastran_test PRIVATE
    ${SRC}
    ${SRC}/io
)

# Mesh.h includes <vulkan/vulkan.h>; Mesh.cpp pulls in tinyobjloader (for
# loadObj, unused here but compiled in). Locate the Conan-cached headers/lib so
# the standalone test builds without a full Conan/CMake integration.
file(GLOB TINYOBJ_INC "$ENV{HOME}/.conan2/p/tinyo*/p/include")
file(GLOB TINYOBJ_LIBDIR "$ENV{HOME}/.conan2/p/tinyo*/p/lib")
file(GLOB VULKAN_INC "$ENV{HOME}/.conan2/p/vulka*/p/include")
find_library(TINYOBJ_LIB NAMES tinyobjloader PATHS ${TINYOBJ_LIBDIR} NO_DEFAULT_PATH)
target_include_directories(nastran_test PRIVATE ${TINYOBJ_INC} ${VULKAN_INC})

target_link_libraries(nastran_test PRIVATE ${VTK_LIBRARIES} ${TINYOBJ_LIB})
vtk_module_autoinit(TARGETS nastran_test MODULES ${VTK_LIBRARIES})
