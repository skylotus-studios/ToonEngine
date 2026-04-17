#pragma once

#include "core/input/action_map.h"

namespace BindingIO {

bool Save(const char* path, const InputContext& ctx);
bool Load(const char* path, InputContext& ctx);

// Key/button/axis enum ↔ string conversion (used by save/load and debug UI).
const char* KeyName(Key k);
Key         KeyByName(const char* name);

} // namespace BindingIO
