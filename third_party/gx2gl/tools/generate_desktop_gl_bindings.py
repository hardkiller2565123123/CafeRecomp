import argparse
import pathlib
import re
from typing import Dict, List, Tuple


PROTO_RE = re.compile(
    r"^\s*(?P<ret>[A-Za-z_][A-Za-z0-9_\s\*]*?)\s*"
    r"(?P<name>gl[A-Za-z0-9_]+)\s*\((?P<args>[^;]*)\)\s*;\s*$",
    re.MULTILINE,
)
FUNC_CALL_RE = re.compile(r"\b(gl[A-Z][A-Za-z0-9_]*)\s*\(")
EXCLUDED = {"glWiiULoadShaderGroup", "glWiiULoadShaderGroupGFD"}


def parse_prototypes(header_text: str) -> Dict[str, Tuple[str, str]]:
    prototypes: Dict[str, Tuple[str, str]] = {}
    for match in PROTO_RE.finditer(header_text):
        name = match.group("name").strip()
        return_type = " ".join(match.group("ret").split())
        arguments = " ".join(match.group("args").split())
        prototypes[name] = (return_type, arguments)
    return prototypes


def extract_function_names(test_texts: List[str]) -> List[str]:
    names = set()
    for test_text in test_texts:
        names.update(FUNC_CALL_RE.findall(test_text))
    return sorted(name for name in names if name not in EXCLUDED)


def split_args(arguments: str) -> List[str]:
    arguments = arguments.strip()
    if not arguments or arguments == "void":
        return []
    return [piece.strip() for piece in arguments.split(",")]


def arg_name(argument: str) -> str:
    token = argument.split()[-1]
    return token.lstrip("*").rstrip("[]")


def default_return(return_type: str) -> str:
    if "*" in return_type:
        return "NULL"
    if return_type == "GLboolean":
        return "GL_FALSE"
    if return_type in {"GLenum", "GLuint", "GLbitfield"}:
        return "0u"
    if return_type in {"GLint", "GLsizei", "GLintptr", "GLsizeiptr", "GLint64"}:
        return "0"
    if return_type == "GLuint64":
        return "0ull"
    if return_type in {"GLfloat", "GLclampf"}:
        return "0.0f"
    if return_type in {"GLdouble", "GLclampd"}:
        return "0.0"
    return "0"


def generate_header() -> str:
    return """#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern \"C\" {
#endif

bool DesktopGLLoadTestEntryPoints(void);
void DesktopGLResetMissingProc(void);
const char *DesktopGLGetLastMissingProc(void);

#ifdef __cplusplus
}
#endif
"""


def generate_source(
    names: List[str], prototypes: Dict[str, Tuple[str, str]]
) -> str:
    lines = [
        '#include "desktop_gl_bindings.h"',
        '#include "desktop_wgl_platform.h"',
        '#include "gl/gl.h"',
        "",
        "#include <windows.h>",
        "#include <string.h>",
        "",
        "static const char *g_desktop_gl_last_missing_proc = NULL;",
        "",
        "static void desktop_gl_note_missing_proc(const char *name) {",
        "    if (!g_desktop_gl_last_missing_proc) {",
        "        g_desktop_gl_last_missing_proc = name;",
        "    }",
        "}",
        "",
        "void DesktopGLResetMissingProc(void) {",
        "    g_desktop_gl_last_missing_proc = NULL;",
        "}",
        "",
        "const char *DesktopGLGetLastMissingProc(void) {",
        "    return g_desktop_gl_last_missing_proc;",
        "}",
        "",
    ]

    for name in names:
        return_type, arguments = prototypes[name]
        lines.extend(
            [
                f"typedef {return_type} (APIENTRY *PFN_{name})({arguments});",
                f"static PFN_{name} g_{name} = NULL;",
                "",
            ]
        )

    lines.extend(["bool DesktopGLLoadTestEntryPoints(void) {", "    bool ok = true;", "    DesktopGLResetMissingProc();"])
    for name in names:
        lines.append(f'    g_{name} = (PFN_{name})DesktopGLPlatformGetProcAddress("{name}");')
        lines.append(f"    if (!g_{name}) ok = false;")
    lines.extend(["    return ok;", "}", ""])

    for name in names:
        return_type, arguments = prototypes[name]
        call_args = ", ".join(arg_name(arg) for arg in split_args(arguments))
        lines.append(f"{return_type} {name}({arguments}) {{")
        lines.append(f"    if (!g_{name}) {{")
        lines.append(f'        desktop_gl_note_missing_proc("{name}");')
        if return_type == "void":
            lines.append("        return;")
        else:
            lines.append(f"        return {default_return(return_type)};")
        lines.append("    }")
        if return_type == "void":
            lines.append(f"    g_{name}({call_args});")
        else:
            lines.append(f"    return g_{name}({call_args});")
        lines.extend(["}", ""])

    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", required=True)
    parser.add_argument("--test-source", dest="test_sources", action="append", required=True)
    parser.add_argument("--out-header", required=True)
    parser.add_argument("--out-source", required=True)
    args = parser.parse_args()

    prototypes = parse_prototypes(pathlib.Path(args.header).read_text(encoding="utf-8"))
    names = extract_function_names(
        [pathlib.Path(path).read_text(encoding="utf-8") for path in args.test_sources]
    )
    missing = [name for name in names if name not in prototypes]
    if missing:
        raise SystemExit("Missing prototypes for: " + ", ".join(missing))

    out_header = pathlib.Path(args.out_header)
    out_source = pathlib.Path(args.out_source)
    out_header.parent.mkdir(parents=True, exist_ok=True)
    out_source.parent.mkdir(parents=True, exist_ok=True)
    out_header.write_text(generate_header(), encoding="utf-8")
    out_source.write_text(generate_source(names, prototypes), encoding="utf-8")


if __name__ == "__main__":
    main()
