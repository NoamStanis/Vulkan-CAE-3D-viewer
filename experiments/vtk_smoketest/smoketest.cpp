// VTK link smoke test — headless data/filter use only.
//
// Goal: prove that VTK links and runs in this toolchain (Apple Clang / libc++)
// WITHOUT instantiating any render window, and that the filter path we plan to
// use for surface extraction works. Builds a tiny unstructured grid of two
// tetrahedra, extracts its free surface, triangulates, and prints the result.
//
// No vtkRenderWindow / vtkActor / vtkRenderer is touched — those would pull in
// VTK's own OpenGL/Qt stack, which we deliberately do not use.

#include <vtkSmartPointer.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>
#include <vtkTetra.h>
#include <vtkCellArray.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkTriangleFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkVersion.h>

#include <cstdio>

int main()
{
    std::printf("VTK version: %s\n", vtkVersion::GetVTKVersion());

    // ── Build a tiny volume mesh: two tetrahedra sharing a face ────────────────
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->InsertNextPoint(0.0, 0.0, 0.0); // 0
    points->InsertNextPoint(1.0, 0.0, 0.0); // 1
    points->InsertNextPoint(0.0, 1.0, 0.0); // 2
    points->InsertNextPoint(0.0, 0.0, 1.0); // 3
    points->InsertNextPoint(1.0, 1.0, 1.0); // 4

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);

    auto tet1 = vtkSmartPointer<vtkTetra>::New();
    tet1->GetPointIds()->SetId(0, 0);
    tet1->GetPointIds()->SetId(1, 1);
    tet1->GetPointIds()->SetId(2, 2);
    tet1->GetPointIds()->SetId(3, 3);

    auto tet2 = vtkSmartPointer<vtkTetra>::New();
    tet2->GetPointIds()->SetId(0, 1);
    tet2->GetPointIds()->SetId(1, 2);
    tet2->GetPointIds()->SetId(2, 3);
    tet2->GetPointIds()->SetId(3, 4);

    grid->Allocate(2);
    grid->InsertNextCell(tet1->GetCellType(), tet1->GetPointIds());
    grid->InsertNextCell(tet2->GetCellType(), tet2->GetPointIds());

    std::printf("input: %lld points, %lld cells\n",
                static_cast<long long>(grid->GetNumberOfPoints()),
                static_cast<long long>(grid->GetNumberOfCells()));

    // ── Free-surface extraction → triangulate → normals ───────────────────────
    auto surface = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surface->SetInputData(grid);

    auto triangles = vtkSmartPointer<vtkTriangleFilter>::New();
    triangles->SetInputConnection(surface->GetOutputPort());

    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputConnection(triangles->GetOutputPort());
    normals->ComputePointNormalsOn();
    normals->Update();

    vtkPolyData *out = normals->GetOutput();
    std::printf("surface: %lld points, %lld triangles\n",
                static_cast<long long>(out->GetNumberOfPoints()),
                static_cast<long long>(out->GetNumberOfPolys()));

    const bool hasNormals = out->GetPointData() &&
                            out->GetPointData()->GetNormals() != nullptr;
    std::printf("point normals present: %s\n", hasNormals ? "yes" : "no");

    if (out->GetNumberOfPolys() == 0) {
        std::printf("FAIL: surface extraction produced no triangles\n");
        return 1;
    }

    std::printf("OK: VTK headless filter pipeline works\n");
    return 0;
}
