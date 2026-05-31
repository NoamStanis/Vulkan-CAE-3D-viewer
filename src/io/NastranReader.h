#pragma once

#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// NastranReader
//
// Hand-written reader for a subset of the Nastran Bulk Data (.bdf) format,
// producing a vtkUnstructuredGrid so the rest of the pipeline (free-surface
// extraction, triangulation, normals) can use VTK filters directly.
//
// Supported in Layer 1 (geometry only):
//   - Field formats: small (8-col fixed), large ('*', 16-col), free (comma).
//   - Continuations across lines.
//   - Comment ('$') and section lines (BEGIN BULK / ENDDATA) ignored.
//   - GRID nodes (coordinate-system field is read but only basic system 0 is
//     honoured; non-zero CP is currently treated as global — see note in .cpp).
//   - Elements: CTETRA, CHEXA, CPENTA (volume) and CTRIA3, CQUAD4 (shell).
//
// Not yet supported: results, materials, loads, INCLUDE files, higher-order
// elements (CTETRA10 etc.). Unknown cards are skipped with a warning count.
//
// Node IDs in the file are arbitrary; they are mapped to dense 0-based VTK point
// indices internally.
// ─────────────────────────────────────────────────────────────────────────────
class NastranReader
{
public:
    struct Stats
    {
        int nodes        = 0;
        int volumeCells  = 0;
        int shellCells   = 0;
        int skippedCards = 0;  // recognised-name cards we couldn't fully parse
        int unknownCards = 0;  // cards whose name we don't handle
    };

    // Reads `path` and returns the model as an unstructured grid. Throws
    // std::runtime_error if the file can't be opened or contains no elements.
    // `outStats`, if non-null, receives parse statistics.
    static vtkSmartPointer<vtkUnstructuredGrid> read(const std::string &path,
                                                     Stats *outStats = nullptr);
};
