#pragma once

#include "Mesh.h"

#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

// ─────────────────────────────────────────────────────────────────────────────
// VtkSurface
//
// Bridges the VTK data layer to the renderer's CPU mesh. Takes a volume/shell
// unstructured grid (e.g. from NastranReader), extracts its free surface,
// triangulates it, generates normals, and converts the result into the
// renderer-native MeshData (interleaved position+normal vertices + indices).
//
// VTK is used headlessly here — only data/filter classes, no rendering.
// ─────────────────────────────────────────────────────────────────────────────
namespace VtkSurface {

// Extract the renderable triangle surface of `grid` as MeshData.
// Throws std::runtime_error if the grid yields no surface triangles.
MeshData toMeshData(vtkUnstructuredGrid *grid);

// Extract the true element-face edges of `grid` as a line-list EdgeData. Edges
// are taken from the surface *before* triangulation, so quad faces yield 4 edges
// (no triangulation diagonals). Returns empty EdgeData if there are no edges.
EdgeData extractEdges(vtkUnstructuredGrid *grid);

} // namespace VtkSurface
