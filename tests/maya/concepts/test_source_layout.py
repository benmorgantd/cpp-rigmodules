"""Source layout: C++ lives under cpp/, core/ stays Maya-free, includes follow the path rules (D13, D22, D23),
include blocks are sorted, and every brace sits on its own line (D14).

These read the source tree, not the plugin, so they never touch the Maya scene. They run in the
registration tier because a broken boundary is a build-structure problem, like a missing node type.
"""
import os
import re
import unittest

from rigtest.config import REPO_DIR

CPP_DIR = os.path.join(REPO_DIR, "cpp")
CORE_DIR = os.path.join(CPP_DIR, "core")
MAYA_DIR = os.path.join(CPP_DIR, "maya")
SOURCE_EXTENSIONS = (".h", ".cpp")
# Every folder that may hold source, relative to cpp/. A new folder is a layout decision, so it is added here on purpose.
SOURCE_FOLDERS = {"core", "maya", "maya/commands", "maya/modules", "maya/nodes", "maya/shared"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
# Third-party headers core may use. The standard library is matched separately (no folder, no extension).
CORE_ALLOWED_SYSTEM_PREFIXES = ("nlohmann/",)

# Brace placement (Allman, D14). Braced init lists on one line ({ a, b }, x{ 1 }, an empty {} argument) are
# values, not blocks, and are allowed. Everything else that opens or closes a block must be alone on its line.
BRACE_VIOLATIONS = [
    ("opening brace after code", re.compile(r"\S.*\{\s*$")),                      # if (x) {   class A {   = {
    ("one-line body", re.compile(r"\)[\w\s:&]*\{[^{}]*;\s*\}")),                  # f() { return x; }
    ("empty one-line body", re.compile(r"\)[\w\s:&]*\{\s*\}\s*;?\s*$")),           # X::X() {}
    ("code after closing brace", re.compile(r"^\s*\}\s*(else|catch|while)\b")),    # } else
]
LINE_COMMENT_RE = re.compile(r"//.*$")
INCLUDE_LINE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')


def include_sort_key(kind, header, file_stem, is_source):
    """The order cpp/.clang-format gives (SortIncludes: CaseInsensitive, IncludeBlocks: Preserve, LLVM categories):
    a .cpp file's own header first, then quoted before angle-bracket includes, then case-insensitive by path."""
    is_main = is_source and os.path.splitext(os.path.basename(header))[0] == file_stem
    return (0 if is_main else 1, 0 if kind == '"' else 1, header.lower(), header)


def source_files(folder):
    """Source paths under folder, relative to cpp/ (the include root) with forward slashes."""
    found = []
    for root, _, files in os.walk(folder):
        for name in files:
            if name.endswith(SOURCE_EXTENSIONS):
                found.append(os.path.relpath(os.path.join(root, name), CPP_DIR).replace("\\", "/"))
    return sorted(found)


def includes(relative_path):
    with open(os.path.join(CPP_DIR, relative_path), encoding="utf-8-sig") as handle:
        return INCLUDE_RE.findall(handle.read())


class SourceLayout(unittest.TestCase):

    def test_source_only_in_known_folders(self):
        # Walks the whole repo, so a .h or .cpp outside cpp/ is caught too. cpp/include is vendored code.
        allowed = {"cpp/" + f for f in SOURCE_FOLDERS}
        stray = []
        for root, dirs, files in os.walk(REPO_DIR):
            folder = os.path.relpath(root, REPO_DIR).replace("\\", "/")
            dirs[:] = [d for d in dirs if not d.startswith(".") and d not in ("x64", "RigModules")
                       and "%s/%s" % (folder, d) != "cpp/include"]
            if folder not in allowed:
                stray += ["%s/%s" % (folder, f) for f in files if f.endswith(SOURCE_EXTENSIONS)]
        self.assertEqual(stray, [], "source files belong in one of %s" % sorted(allowed))

    def test_core_includes_only_core_std_and_nlohmann(self):
        # Core includes its own headers by bare name, so core/ stays self-contained if it becomes its own library.
        core_names = {os.path.basename(p) for p in source_files(CORE_DIR)}
        bad = []
        for path in source_files(CORE_DIR):
            for kind, header in includes(path):
                if kind == '"':
                    allowed = header in core_names
                else:
                    is_std = "/" not in header and "." not in header
                    allowed = is_std or header.startswith(CORE_ALLOWED_SYSTEM_PREFIXES)
                if not allowed:
                    bad.append("%s: %s" % (path, header))
        self.assertEqual(bad, [], "core/ must not include Maya or plugin headers")

    def test_maya_includes_by_path_from_cpp(self):
        # "core/Constants.h", "maya/shared/RigModule.h" (relative to cpp/): the include line says where the header lives,
        # and a file can move between maya/ folders without its includers guessing the new relative path.
        known = set(source_files(CORE_DIR)) | set(source_files(MAYA_DIR))
        bad = []
        for path in source_files(MAYA_DIR):
            for kind, header in includes(path):
                if kind == '"' and header not in known:
                    bad.append("%s: %s" % (path, header))
        self.assertEqual(bad, [], 'maya/ includes project headers by path from cpp/, e.g. "maya/shared/RigModule.h"')

    def test_braces_on_their_own_lines(self):
        # A cheap line-based check, not a parser. It catches the patterns clang-format's Allman setting would
        # rewrite, so code that passes here is unchanged by .clang-format's brace rules.
        bad = []
        for path in source_files(CORE_DIR) + source_files(MAYA_DIR):
            with open(os.path.join(CPP_DIR, path), encoding="utf-8-sig") as handle:
                for number, line in enumerate(handle, 1):
                    code = LINE_COMMENT_RE.sub("", line).rstrip()
                    for label, pattern in BRACE_VIOLATIONS:
                        if pattern.search(code):
                            bad.append("%s:%d %s: %s" % (path, number, label, code.strip()))
        self.assertEqual(bad, [], "every brace that opens or closes a block goes on its own line")

    def test_include_blocks_sorted(self):
        # A block is a run of consecutive #include lines. Blank lines and comments separate the hand-written groups
        # (own header, project, standard library, Maya), and only the order inside a block is checked.
        bad = []
        for path in source_files(CORE_DIR) + source_files(MAYA_DIR):
            stem, extension = os.path.splitext(os.path.basename(path))
            block = []
            with open(os.path.join(CPP_DIR, path), encoding="utf-8-sig") as handle:
                for line in list(handle) + [""]:
                    match = INCLUDE_LINE_RE.match(line)
                    if match:
                        block.append(include_sort_key(match.group(1), match.group(2), stem, extension == ".cpp"))
                        continue
                    if block != sorted(block):
                        bad.append("%s: %s" % (path, ", ".join(key[3] for key in block)))
                    block = []
        self.assertEqual(bad, [], "include blocks are sorted; run clang-format on the include lines (cpp/.clang-format)")
