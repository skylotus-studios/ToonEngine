#!/usr/bin/env python3
"""Static checks for the CLAUDE.md hard rules a script can actually decide.

scripts/verify.py's fast-tier `invariants` step. Pure stdlib, matching every other script in
scripts/, and deliberately build-free: it runs in milliseconds and sits FIRST in the fast tier,
so a seam violation fails in a second instead of after a full build.

Three of the six hard rules are mechanically checkable. This checks those three and claims
nothing about the other three -- see docs/invariants.md for which rule is enforced by what, and
why rules 1 and 6 stay review-only.

  seams   (rule 2) Diligent, Jolt, and miniaudio stay inside a named list of implementation
                   TUs. Every header under src/, and every other TU, must be free of them.
  shaders (rule 3) every shader source in the tree is HLSL.
  cmake   (rules 4 and 5) DILIGENT_NO_* are cache-forced before DiligentCore is added, the
                   C++ standard is 17, and no directory-scoped include/link command appears in
                   CMakeLists.txt.

Every check reports all of its violations, not just the first, so one run tells you the whole
story. Exit 0 clean, 1 with findings.
"""
import argparse
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# --- rule 2: the seams --------------------------------------------------------------------------

# The ONLY translation units allowed to include a backend header or name a backend type. Adding
# a file here is the deliberate, reviewable act rule 2 is about; it is not a list to grow to make
# a build pass. Headers are never exempt, not even the ones sitting next to these files:
# renderer.h is checked, renderer.cpp is not.
BACKEND_TUS = {
    "src/core/rendering/renderer.cpp": "Diligent (the rendering seam's implementation)",
    "src/core/scene/scene.cpp": "Diligent (float4x4 world-transform composition)",
    "src/core/camera/camera.cpp": "Diligent (float4x4 camera basis)",
    "src/core/physics/physics.cpp": "Jolt (the physics seam's implementation)",
    "src/core/audio/audio.cpp": "miniaudio (the audio seam's implementation)",
    "src/core/audio/miniaudio_impl.cpp": "miniaudio (compiles the single-header implementation)",
}

# Quoted includes a checked file may use: its own project headers, plus Dear ImGui and the plain
# ImGui add-ons rule 2 explicitly exempts. Everything else quoted is a vendored header.
#
# This is an allow-list on purpose. Every Diligent header is a bare quoted name with no
# distinguishing prefix (BasicMath.hpp, RenderDevice.h, Shaders/Common/public/BasicStructures.fxh,
# Utilities/interface/...), so a deny-list would only ever catch the headers someone thought to
# list. An allow-list fails CLOSED: a Diligent header nobody has seen before is caught on sight.
PROJECT_INCLUDE_ROOTS = ("core/", "app/", "ui/")
IMGUI_EXEMPT_INCLUDES = {
    "imgui.h",
    "imgui_internal.h",
    "ImGuizmo.h",              # a plain ImGui add-on, not a Diligent type
    "IconsFontAwesome6.h",     # glyph-name macros, header-only
    "backends/imgui_impl_glfw.h",
}
# Angle-bracket includes are STL, GLFW, nlohmann/json, and Win32 -- all portable or already
# accounted for. Jolt is the one vendored library that spells itself this way.
FORBIDDEN_ANGLE_PREFIXES = ("Jolt/",)
# A backstop for the include allow-list: a qualified backend name in code that somehow got its
# header in anyway (a transitive include, a forward declaration).
FORBIDDEN_TOKENS = ("Diligent::", "JPH::", "using namespace Diligent")

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
_CHAR_LITERAL_RE = re.compile(r"'(?:[^'\\\n]|\\.)'")


def strip_comments_and_strings(text):
    """Returns (code, bare): `code` has comments blanked out, `bare` also has string and char
    literal bodies blanked. Line count and column offsets are preserved in both, so a match's
    line number still points at the real line.

    Needed because grepping raw source for "Diligent" finds 17 hits in src/app and src/ui that
    are all prose in comments. A checker that cannot tell a comment from code is a checker
    nobody will leave switched on."""
    code, bare = [], []
    i, n = 0, len(text)

    def blank(chunk):
        return "".join(ch if ch == "\n" else " " for ch in chunk)

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            code.append(blank(text[i:j]))
            bare.append(blank(text[i:j]))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            code.append(blank(text[i:j]))
            bare.append(blank(text[i:j]))
            i = j
        elif c == "R" and nxt == '"':
            # Raw string R"delim( ... )delim" -- its body can hold anything, including the
            # quotes and slashes the states above key off (renderer.cpp embeds shader source
            # this way), so it has to be consumed whole.
            open_paren = text.find("(", i + 2)
            if open_paren < 0:
                code.append(c)
                bare.append(c)
                i += 1
                continue
            close = ")" + text[i + 2:open_paren] + '"'
            j = text.find(close, open_paren + 1)
            j = n if j < 0 else j + len(close)
            code.append(text[i:j])
            bare.append(blank(text[i:j]))
            i = j
        elif c == "'" and _CHAR_LITERAL_RE.match(text, i):
            # Only a real char literal ('x', '\n'). Bailing out on anything else keeps C++14
            # digit separators (1'000'000) from swallowing the rest of the file.
            j = _CHAR_LITERAL_RE.match(text, i).end()
            code.append(text[i:j])
            bare.append(blank(text[i:j]))
            i = j
        elif c == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                if text[j] == "\n":
                    break
                j += 1
            code.append(text[i:j])
            bare.append(blank(text[i:j]))
            i = j
        else:
            code.append(c)
            bare.append(c)
            i += 1
    return "".join(code), "".join(bare)


def _line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def check_seams(violations):
    src = REPO_ROOT / "src"
    for path in sorted(src.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp"):
            continue
        rel = path.relative_to(REPO_ROOT).as_posix()
        # A backend TU is exempt from the include and token rules. Its own HEADER never is.
        if rel in BACKEND_TUS:
            continue

        raw = path.read_text(encoding="utf-8", errors="replace")
        code, bare = strip_comments_and_strings(raw)

        for match in _INCLUDE_RE.finditer(code):
            bracket, target = match.group(1), match.group(2)
            line = _line_of(code, match.start())
            if bracket == "<":
                for prefix in FORBIDDEN_ANGLE_PREFIXES:
                    if target.startswith(prefix):
                        violations.append(f"{rel}:{line}: includes <{target}> -- Jolt belongs "
                                          f"only in src/core/physics/physics.cpp (rule 2)")
                continue
            if target in IMGUI_EXEMPT_INCLUDES:
                continue
            if target.startswith(PROJECT_INCLUDE_ROOTS):
                continue
            violations.append(
                f'{rel}:{line}: includes "{target}" -- not a project header (core/, app/, ui/) '
                f"and not one of the ImGui headers rule 2 exempts. If this is a Diligent, Jolt, "
                f"or miniaudio header it belongs behind a seam; if it is a new exemption, add it "
                f"to scripts/check_invariants.py deliberately")

        for token in FORBIDDEN_TOKENS:
            start = 0
            while True:
                found = bare.find(token, start)
                if found < 0:
                    break
                violations.append(f"{rel}:{_line_of(bare, found)}: names `{token}` in code -- "
                                  f"backend types stay behind the seam (rule 2)")
                start = found + len(token)


# --- rule 3: HLSL only --------------------------------------------------------------------------

HLSL_SUFFIXES = {".hlsl", ".hlsli"}
NON_HLSL_SHADER_SUFFIXES = {".glsl", ".vert", ".frag", ".geom", ".comp", ".tesc", ".tese",
                            ".spv", ".metal", ".msl", ".cg", ".wgsl"}
SKIP_DIRS = {"external", "build", "artifacts", ".git", ".idea", "cmake-build-debug",
             "cmake-build-release"}


def check_shaders(violations):
    shader_dir = REPO_ROOT / "assets/shaders"
    for path in sorted(shader_dir.rglob("*")):
        if path.is_file() and path.suffix not in HLSL_SUFFIXES:
            rel = path.relative_to(REPO_ROOT).as_posix()
            violations.append(f"{rel}: not HLSL -- assets/shaders/ holds .hlsl and .hlsli only "
                              f"(rule 3)")

    # A shader smuggled in somewhere other than assets/shaders/ counts too. os.walk with in-place
    # pruning rather than rglob: external/ alone is tens of thousands of vendored files, and
    # filtering them out AFTER the walk costs seconds on a check that has to stay free enough to
    # sit first in the fast tier. That directory is pruned as well, so a stray file there is
    # reported once, by the loop above, with the more specific message.
    for dirpath, dirnames, filenames in os.walk(REPO_ROOT):
        here = Path(dirpath)
        dirnames[:] = [d for d in sorted(dirnames)
                       if d not in SKIP_DIRS and here / d != shader_dir]
        for filename in sorted(filenames):
            if Path(filename).suffix.lower() in NON_HLSL_SHADER_SUFFIXES:
                rel = (here / filename).relative_to(REPO_ROOT).as_posix()
                violations.append(f"{rel}: non-HLSL shader source -- shaders are HLSL, "
                                  f"cross-compiled to SPIR-V by Diligent at runtime (rule 3)")


# --- rules 4 and 5: the build ---------------------------------------------------------------------

REQUIRED_DILIGENT_DISABLES = ("DILIGENT_NO_DIRECT3D11", "DILIGENT_NO_DIRECT3D12",
                              "DILIGENT_NO_OPENGL", "DILIGENT_NO_RADIENT")
# Directory-scoped commands that leak a setting onto every target defined afterwards, including
# the vendored submodules. add_compile_options is deliberately NOT here: the ASan block needs
# exactly that whole-tree reach (a mismatched sanitizer runtime between TUs silently stops
# catching anything), and the guard below checks it stays inside that block.
BANNED_DIRECTORY_COMMANDS = ("include_directories", "link_libraries", "link_directories",
                             "add_definitions")


def check_cmake(violations):
    path = REPO_ROOT / "CMakeLists.txt"
    rel = "CMakeLists.txt"
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    # CMake comments are # to end of line, and this file's banners talk about the very commands
    # being checked for -- strip them before matching anything.
    code_lines = [line.split("#", 1)[0] for line in lines]

    core_line = next((i for i, line in enumerate(code_lines)
                      if "add_subdirectory(external/DiligentCore)" in line), None)
    if core_line is None:
        violations.append(f"{rel}: no add_subdirectory(external/DiligentCore) found -- this "
                          f"check cannot verify rule 4's ordering")
        core_line = len(code_lines)

    seen_disables = set()
    for i, line in enumerate(code_lines):
        match = re.match(r"\s*set\(\s*(DILIGENT_NO_\w+)", line)
        if not match:
            continue
        name = match.group(1)
        seen_disables.add(name)
        if not ("CACHE" in line and "BOOL" in line and "FORCE" in line):
            violations.append(f"{rel}:{i + 1}: set({name} ...) is not `CACHE BOOL \"\" FORCE` -- "
                              f"a plain set() loses to the submodule's own option() (rule 4)")
        if i > core_line:
            violations.append(f"{rel}:{i + 1}: set({name} ...) comes after "
                              f"add_subdirectory(external/DiligentCore) on line {core_line + 1} "
                              f"-- too late to affect the cache (rule 4)")

    for name in REQUIRED_DILIGENT_DISABLES:
        if name not in seen_disables:
            violations.append(f"{rel}: {name} is no longer set. Re-enabling a backend or module "
                              f"is allowed, but rule 4 wants a stated reason and a docs/"
                              f"invariants.md update, not a silent drop")

    asan_depth = 0
    for i, line in enumerate(code_lines):
        stripped = line.strip()
        if re.match(r"if\s*\(\s*TOONENGINE_ASAN\s*\)", stripped):
            asan_depth += 1
        elif asan_depth and re.match(r"endif\s*\(", stripped):
            asan_depth -= 1
        for command in BANNED_DIRECTORY_COMMANDS:
            if re.match(rf"{command}\s*\(", stripped):
                violations.append(f"{rel}:{i + 1}: {command}() is directory-scoped -- use the "
                                  f"target_* form so the setting lands on one target instead of "
                                  f"every target defined after it (rule 5)")
        if re.match(r"add_compile_options\s*\(", stripped) and not asan_depth:
            violations.append(f"{rel}:{i + 1}: add_compile_options() outside the "
                              f"if(TOONENGINE_ASAN) block. Whole-tree reach is the sanitizer's "
                              f"one stated exception to rule 5; anything else uses "
                              f"target_compile_options()")

    if not any(re.match(r"\s*set\(CMAKE_CXX_STANDARD\s+17\s*\)", line) for line in code_lines):
        violations.append(f"{rel}: set(CMAKE_CXX_STANDARD 17) not found (rule 5)")

    for name in ("CMakeLists.txt", "CMakePresets.json"):
        text = (REPO_ROOT / name).read_text(encoding="utf-8", errors="replace")
        code = "\n".join(line.split("#", 1)[0] for line in text.splitlines())
        if "vcpkg" in code.lower():
            violations.append(f"{name}: mentions vcpkg -- dependencies are git submodules under "
                              f"external/ (rule 5)")


# --- driver ----------------------------------------------------------------------------------------

CHECKS = {
    "seams": check_seams,
    "shaders": check_shaders,
    "cmake": check_cmake,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="append", choices=sorted(CHECKS), default=[],
                        help="run only this check; repeatable (default: all three)")
    args = parser.parse_args()

    selected = args.check or sorted(CHECKS)
    total = 0
    for name in selected:
        violations = []
        CHECKS[name](violations)
        total += len(violations)
        if violations:
            for line in violations:
                print(f"check_invariants: {name}: {line}", file=sys.stderr)
        else:
            print(f"check_invariants: {name}: OK")

    if total:
        print(f"\ncheck_invariants: FAIL -- {total} violation(s). These are CLAUDE.md's hard "
              f"rules; see docs/invariants.md for the reasoning before changing either the code "
              f"or this script.", file=sys.stderr)
        return 1
    print(f"check_invariants: OK -- {len(selected)}/{len(selected)} check(s) passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
