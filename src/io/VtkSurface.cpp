#include "VtkSurface.h"

#include <vtkDataSetSurfaceFilter.h>
#include <vtkTriangleFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyData.h>
#include <vtkCellArray.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkPoints.h>

#include <stdexcept>

namespace VtkSurface {

MeshData toMeshData(vtkUnstructuredGrid *grid)
{
    if (!grid)
        throw std::runtime_error("VtkSurface: null grid");

    // Free-surface extraction → triangulate → per-point normals. This is the
    // same filter chain proven in the smoke test; here the output is consumed
    // rather than just counted.
    auto surface = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surface->SetInputData(grid);

    auto triangles = vtkSmartPointer<vtkTriangleFilter>::New();
    triangles->SetInputConnection(surface->GetOutputPort());

    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputConnection(triangles->GetOutputPort());
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->SplittingOff();        // keep point count stable (no normal-based splits)
    normals->ConsistencyOn();       // coherent winding across the surface
    normals->AutoOrientNormalsOn(); // point normals outward where possible
    normals->Update();

    vtkPolyData *poly = normals->GetOutput();
    const vtkIdType nPts  = poly->GetNumberOfPoints();
    const vtkIdType nTris = poly->GetNumberOfPolys();
    if (nPts == 0 || nTris == 0)
        throw std::runtime_error("VtkSurface: grid produced no surface triangles");

    MeshData mesh;
    mesh.vertices.reserve(static_cast<size_t>(nPts));
    mesh.indices.reserve(static_cast<size_t>(nTris) * 3);

    vtkDataArray *normalArray = poly->GetPointData()
                                    ? poly->GetPointData()->GetNormals()
                                    : nullptr;

    // ── Vertices ──────────────────────────────────────────────────────────────
    for (vtkIdType i = 0; i < nPts; ++i) {
        double p[3];
        poly->GetPoint(i, p);

        Vertex v{};
        v.position[0] = static_cast<float>(p[0]);
        v.position[1] = static_cast<float>(p[1]);
        v.position[2] = static_cast<float>(p[2]);

        if (normalArray) {
            double n[3];
            normalArray->GetTuple(i, n);
            v.normal[0] = static_cast<float>(n[0]);
            v.normal[1] = static_cast<float>(n[1]);
            v.normal[2] = static_cast<float>(n[2]);
        } else {
            v.normal[0] = 0.0f; v.normal[1] = 0.0f; v.normal[2] = 1.0f;
        }
        mesh.vertices.push_back(v);
    }

    // ── Indices ───────────────────────────────────────────────────────────────
    // vtkTriangleFilter guarantees all polys are triangles, so each cell has
    // exactly 3 point ids.
    auto polys = poly->GetPolys();
    polys->InitTraversal();
    auto idList = vtkSmartPointer<vtkIdList>::New();
    while (polys->GetNextCell(idList)) {
        if (idList->GetNumberOfIds() != 3)
            continue;  // defensive; shouldn't happen after vtkTriangleFilter
        mesh.indices.push_back(static_cast<uint32_t>(idList->GetId(0)));
        mesh.indices.push_back(static_cast<uint32_t>(idList->GetId(1)));
        mesh.indices.push_back(static_cast<uint32_t>(idList->GetId(2)));
    }

    return mesh;
}

} // namespace VtkSurface
