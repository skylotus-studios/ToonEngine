#pragma once
//============================================================================
//  core/input/binding_io.h — JSON save/load for an InputContext's bindings.
//
//  Human-editable persistence so "rebinding" means editing assets/input.json (or a future
//  in-editor panel) rather than recompiling. JSON via nlohmann/json, already vendored by
//  DiligentTools (Diligent-JSON — see CMakeLists.txt), so this adds no new dependency.
//  Ported from ToonEngineOld/src/core/input/binding_io.h.
//============================================================================
#include "core/input/action_map.h"

namespace toon { namespace Input { namespace BindingIO {

// Write `ctx`'s actions/axes to `path` as JSON. Returns false (and logs to stderr) if the
// file can't be opened for writing.
bool Save(const char* path, const InputContext& ctx);

// Replace `ctx`'s actions/axes with the ones loaded from `path`. Returns false (and logs to
// stderr, leaving `ctx` untouched — no partial overwrite) if the file doesn't exist or fails
// to parse.
bool Load(const char* path, InputContext& ctx);

// Key enum <-> string name, exposed for callers building their own UI/diagnostics.
const char* KeyName(Key k);
Key         KeyByName(const char* name);

}}} // namespace toon::Input::BindingIO
