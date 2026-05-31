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

add_executable(nastran_test
    nastran_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/io/NastranReader.cpp
)
target_include_directories(nastran_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/io
)
target_link_libraries(nastran_test PRIVATE ${VTK_LIBRARIES})
vtk_module_autoinit(TARGETS nastran_test MODULES ${VTK_LIBRARIES})
