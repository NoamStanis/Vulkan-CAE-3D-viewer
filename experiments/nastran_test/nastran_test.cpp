// Standalone test for NastranReader (Layer 1) + a Layer-2 surface-extraction
// sanity check. Not part of the app build; run from this experiments dir.

#include "NastranReader.h"

#include <vtkDataSetSurfaceFilter.h>
#include <vtkTriangleFilter.h>
#include <vtkPolyData.h>

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool cond, const char *what)
{
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++g_failures;
}

static void dumpStats(const NastranReader::Stats &s)
{
    std::printf("    nodes=%d volume=%d shell=%d skipped=%d unknown=%d\n",
                s.nodes, s.volumeCells, s.shellCells, s.skippedCards, s.unknownCards);
}

int main(int argc, char **argv)
{
    const std::string dataDir = (argc > 1) ? argv[1] : "../../test/data/";

    // ── Deck 1: small-field tet + shells ───────────────────────────────────────
    std::printf("== tet_and_shells.bdf ==\n");
    {
        NastranReader::Stats s;
        auto grid = NastranReader::read(dataDir + "tet_and_shells.bdf", &s);
        dumpStats(s);
        check(s.nodes == 6,       "6 GRID nodes");
        check(s.volumeCells == 1, "1 CTETRA");
        check(s.shellCells == 2,  "2 shells (CTRIA3 + CQUAD4)");
        check(grid->GetNumberOfCells() == 3, "3 cells total in grid");

        // First node should be at the origin.
        double p0[3];
        grid->GetPoint(0, p0);
        check(p0[0] == 0.0 && p0[1] == 0.0 && p0[2] == 0.0, "node 1 at origin");
    }

    // ── Deck 2: large-field + free-field formats ───────────────────────────────
    std::printf("== formats.bdf ==\n");
    {
        NastranReader::Stats s;
        auto grid = NastranReader::read(dataDir + "formats.bdf", &s);
        dumpStats(s);
        check(s.nodes == 5,       "5 nodes (4 large-field + 1 free-field)");
        check(s.volumeCells == 1, "1 free-field CTETRA");

        // Large-field node 101 was at (2.0, 0.0, 0.0); it is the 2nd inserted.
        double p1[3];
        grid->GetPoint(1, p1);
        check(p1[0] == 2.0 && p1[1] == 0.0 && p1[2] == 0.0,
              "large-field node 101 at (2,0,0)");

        // Free-field node 200 at (3,3,3) is the 5th inserted.
        double p4[3];
        grid->GetPoint(4, p4);
        check(p4[0] == 3.0 && p4[1] == 3.0 && p4[2] == 3.0,
              "free-field node 200 at (3,3,3)");
    }

    // ── Layer-2 sanity: surface extraction on the tet deck ─────────────────────
    std::printf("== surface extraction ==\n");
    {
        auto grid = NastranReader::read(dataDir + "tet_and_shells.bdf");
        auto surface = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
        surface->SetInputData(grid);
        auto tris = vtkSmartPointer<vtkTriangleFilter>::New();
        tris->SetInputConnection(surface->GetOutputPort());
        tris->Update();
        const vtkIdType nTris = tris->GetOutput()->GetNumberOfPolys();
        std::printf("    surface triangles=%lld\n", static_cast<long long>(nTris));
        check(nTris > 0, "surface extraction yields triangles");
    }

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
