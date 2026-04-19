// Mesh types — thin re-export from the renderer abstraction.
//
// MeshHandle and VertexAttrib are defined in renderer.h. This header
// exists so callers that historically included "core/mesh.h" continue
// to compile without change.

#pragma once

#include "core/renderer.h"

using Mesh = MeshHandle;
