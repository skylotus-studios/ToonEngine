#pragma once
//============================================================================
//  core/scene/scripts/builtin_scripts.h: register every script type this build ships with.
//
//  Scripts used to self-register from a file-scope static in their own .cpp. That silently does
//  not work: every script TU lives in ToonRuntime, a STATIC library, and a linker only pulls an
//  object file out of a static library when something references a symbol in it. Registration
//  happened purely as a side effect of a static initializer, so nothing referenced those TUs and
//  the linker discarded them whole -- the registry came up empty and loading a scene logged
//  "Unknown script type 'X'" and dropped the behavior.
//
//  It was invisible until roadmap #19 because only ToonEngine.exe was affected in practice, and
//  there app/editor_init.cpp happens to `make_unique<SpinScript>()` for the demo scene, which is
//  a real symbol reference that dragged that one TU in. ToonPlayer.exe links no such reference,
//  so a shipped game silently ran no scripts at all -- the same class of defect as the runtime
//  never building a physics or audio world.
//
//  The fix is to stop depending on static-initializer side effects across a library boundary and
//  call registration explicitly. Adding a script type means adding one line to
//  RegisterBuiltinScripts, which is also the one place to look to see what a build ships with.
//============================================================================
namespace toon {

    // Register every built-in script type with the name registry (core/scene/script.h). Call once
    // at startup, before any scene is loaded: app/runtime_init.cpp's InitRuntime and
    // app/editor_init.cpp's InitEditor both do. Safe to call more than once (each registration
    // just overwrites its own name).
    void RegisterBuiltinScripts();

} // namespace toon
