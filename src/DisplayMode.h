#pragma once

// How the model surface and its element edges are drawn. Shared between the
// renderer and ViewerItem/QML.
enum class DisplayMode
{
    Shaded = 0,    // lit surface only
    ShadedEdges,   // lit surface with element edges overlaid
    Wireframe,     // element edges only (no surface fill)

    Count          // number of modes (for cycling)
};
