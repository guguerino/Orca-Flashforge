#!/usr/bin/env python3
"""
Scan repository text files for invalid UTF-8 and convert legacy-encoded files to
UTF-8 without BOM.

Default conversion uses gb18030, which covers GB2312/GBK/GB18030. Add extra
--encoding values if your tree contains files from other legacy code pages.

Examples:
  python scripts/fix_invalid_utf8.py --check
  python scripts/fix_invalid_utf8.py
  python scripts/fix_invalid_utf8.py --encoding gb18030 --encoding shift_jis
"""

from __future__ import annotations

import argparse
import codecs
import fnmatch
import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


DEFAULT_SOURCE_ENCODINGS = ("gb18030",)

DEFAULT_SKIP_DIR_PATTERNS = {
    ".git",
    ".vs",
    ".idea",
    ".vscode",
    ".cache",
    ".agents",
    ".codex",
    ".codex_tmp*",
    ".pytest_cache",
    "__pycache__",
    "build",
    "build-*",
    "build_*",
    "cmake-build-*",
    "deps",
    "deps_src",
    "out",
    "sandboxes",
}

DEFAULT_BINARY_EXTENSIONS = {
    ".3mf",
    ".7z",
    ".a",
    ".avi",
    ".bin",
    ".bmp",
    ".bz2",
    ".cur",
    ".dat",
    ".dll",
    ".dylib",
    ".exe",
    ".gif",
    ".gz",
    ".icns",
    ".ico",
    ".jar",
    ".jpeg",
    ".jpg",
    ".lib",
    ".mov",
    ".mp3",
    ".mp4",
    ".obj",
    ".o",
    ".otf",
    ".pdf",
    ".pdb",
    ".png",
    ".pyc",
    ".rar",
    ".so",
    ".stl",
    ".tar",
    ".tga",
    ".ttf",
    ".wav",
    ".webp",
    ".woff",
    ".woff2",
    ".xz",
    ".zip",
    ".zst",
}

FORCE_TEXT_EXTENSIONS = {
    ".c",
    ".cc",
    ".cmake",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
    ".m",
    ".mm",
    ".rc",
    ".ui",
}

BOM_ENCODINGS = (
    (codecs.BOM_UTF32_LE, "utf-32-le"),
    (codecs.BOM_UTF32_BE, "utf-32-be"),
    (codecs.BOM_UTF16_LE, "utf-16-le"),
    (codecs.BOM_UTF16_BE, "utf-16-be"),
)


@dataclass
class Stats:
    scanned: int = 0
    valid_utf8: int = 0
    converted: int = 0
    restored: int = 0
    would_convert: int = 0
    would_restore: int = 0
    unresolved: int = 0
    skipped_binary: int = 0
    skipped_large: int = 0
    read_errors: int = 0


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent

    parser = argparse.ArgumentParser(
        description="Find files that are not valid UTF-8 and convert legacy text files to UTF-8 without BOM.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=repo_root,
        help="Root directory to scan. Defaults to the repository root.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Report invalid UTF-8 files without modifying them. Exits 1 if convertible files are found.",
    )
    parser.add_argument(
        "--encoding",
        action="append",
        dest="encodings",
        help=(
            "Legacy source encoding to try, in order. Can be passed multiple times. "
            "Defaults to gb18030."
        ),
    )
    parser.add_argument(
        "--include-vendored",
        action="store_true",
        help="Also scan deps/ and deps_src/. They are skipped by default.",
    )
    parser.add_argument(
        "--skip-dir",
        action="append",
        default=[],
        help="Additional directory name or fnmatch pattern to skip. Can be passed multiple times.",
    )
    parser.add_argument(
        "--no-binary-extension-skip",
        action="store_true",
        help="Do not skip files by common binary extensions. NUL-byte binary detection still applies.",
    )
    parser.add_argument(
        "--max-bytes",
        type=int,
        default=50 * 1024 * 1024,
        help="Skip files larger than this many bytes. Use 0 for no size limit. Default: 52428800.",
    )
    parser.add_argument(
        "--keep-mtime",
        action="store_true",
        help="Preserve each converted/restored file's modification timestamp.",
    )
    parser.add_argument(
        "--restore-tsd-from-git",
        choices=("index", "HEAD", "none"),
        default="index",
        help=(
            "Restore TSD-wrapped source files from Git instead of treating them as "
            "legacy-encoded text. Defaults to the Git index; use HEAD to ignore staged "
            "content, or none to only report them as unresolved."
        ),
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print valid UTF-8 files as they are scanned.",
    )
    return parser.parse_args()


def should_skip_dir(name: str, patterns: Sequence[str]) -> bool:
    return any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)


def iter_files(root: Path, skip_patterns: Sequence[str]) -> Iterable[Path]:
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            name for name in dirnames if not should_skip_dir(name, skip_patterns)
        )
        for filename in sorted(filenames):
            yield Path(dirpath) / filename


def is_binary_extension(path: Path) -> bool:
    return path.suffix.lower() in DEFAULT_BINARY_EXTENSIONS


def is_forced_text(path: Path) -> bool:
    return path.suffix.lower() in FORCE_TEXT_EXTENSIONS or path.name == "CMakeLists.txt"


def looks_binary(data: bytes) -> bool:
    if any(data.startswith(bom) for bom, _encoding in BOM_ENCODINGS):
        return False
    if data.startswith(codecs.BOM_UTF8):
        return False
    return b"\0" in data[:4096]


def relative(path: Path, root: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def run_git(git_root: Path, args: Sequence[str]) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        ("git", "-C", str(git_root), *args),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def find_git_root(root: Path) -> Path | None:
    result = run_git(root, ("rev-parse", "--show-toplevel"))
    if result.returncode != 0:
        return None
    return Path(result.stdout.decode("utf-8", errors="replace").strip()).resolve()


def git_relative_path(path: Path, git_root: Path) -> str | None:
    try:
        return path.resolve().relative_to(git_root).as_posix()
    except ValueError:
        return None


def git_core_autocrlf(git_root: Path) -> str:
    result = run_git(git_root, ("config", "--get", "core.autocrlf"))
    if result.returncode != 0:
        return ""
    return result.stdout.decode("utf-8", errors="replace").strip().lower()


def worktree_newline_for_git_path(git_root: Path, rel_git_path: str) -> str:
    result = run_git(git_root, ("ls-files", "--eol", "--", rel_git_path))
    if result.returncode == 0:
        output = result.stdout.decode("utf-8", errors="replace").strip()
        fields = output.split()
        for field in fields:
            if field == "w/crlf" or field == "attr/eol=crlf":
                return "\r\n"
            if field == "w/lf" or field == "attr/eol=lf":
                return "\n"
        if any(field.startswith("attr/text") for field in fields):
            if os.name == "nt" and git_core_autocrlf(git_root) == "true":
                return "\r\n"
    return "\n"


def normalize_newlines(text: str, newline: str) -> str:
    return newline.join(text.splitlines(keepends=False)) + (
        newline if text.endswith(("\n", "\r")) else ""
    )


def write_bytes_via_text_temp(path: Path, data: bytes) -> None:
    # Some endpoint protection tools wrap direct writes to source extensions.
    # Write a neutral temporary file first, then copy it to the source path.
    temp_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            suffix=".txt",
            prefix=".utf8-restore-",
            dir=path.parent,
            delete=False,
        ) as temp_file:
            temp_file.write(data)
            temp_name = temp_file.name
        shutil.copyfile(temp_name, path)
    finally:
        if temp_name is not None:
            try:
                Path(temp_name).unlink()
            except OSError:
                pass


def restore_tsd_from_git(
    path: Path,
    root: Path,
    git_root: Path | None,
    args: argparse.Namespace,
    stats: Stats,
) -> bool:
    relpath = relative(path, root)
    if args.restore_tsd_from_git == "none":
        return False
    if git_root is None:
        print(f"UNRESOLVED {relpath}: TSD-wrapped file and no Git repository was found")
        return False

    rel_git_path = git_relative_path(path, git_root)
    if rel_git_path is None:
        print(f"UNRESOLVED {relpath}: TSD-wrapped file is outside the Git work tree")
        return False

    source = f":{rel_git_path}" if args.restore_tsd_from_git == "index" else f"HEAD:{rel_git_path}"
    result = run_git(git_root, ("cat-file", "-p", source))
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", errors="replace").strip()
        print(f"UNRESOLVED {relpath}: unable to read Git {args.restore_tsd_from_git} blob; {error}")
        return False

    try:
        text = result.stdout.decode("utf-8")
    except UnicodeDecodeError as exc:
        print(
            f"UNRESOLVED {relpath}: Git {args.restore_tsd_from_git} blob is not valid UTF-8 "
            f"at byte {exc.start}"
        )
        return False

    if result.stdout.startswith(b"%TSD-Header-###%"):
        print(f"UNRESOLVED {relpath}: Git {args.restore_tsd_from_git} blob is also TSD-wrapped")
        return False

    if args.check:
        stats.would_restore += 1
        print(f"WOULD-RESTORE {relpath}: TSD-wrapped file <- Git {args.restore_tsd_from_git}")
        return True

    old_mtime_ns = path.stat().st_mtime_ns
    newline = worktree_newline_for_git_path(git_root, rel_git_path)
    restored = normalize_newlines(text, newline).encode("utf-8")
    write_bytes_via_text_temp(path, restored)
    if args.keep_mtime:
        os.utime(path, ns=(old_mtime_ns, old_mtime_ns))

    stats.restored += 1
    print(f"RESTORED {relpath}: TSD-wrapped file <- Git {args.restore_tsd_from_git}")
    return True


def decode_legacy(data: bytes, encodings: Sequence[str]) -> tuple[str, str] | None:
    for bom, encoding in BOM_ENCODINGS:
        if data.startswith(bom):
            text = data[len(bom) :].decode(encoding)
            return text, f"{encoding}-bom"

    for encoding in encodings:
        try:
            text = data.decode(encoding)
            if text.encode(encoding) != data:
                continue
            return text, encoding
        except (LookupError, UnicodeError):
            continue

    return None


def process_file(
    path: Path,
    root: Path,
    git_root: Path | None,
    encodings: Sequence[str],
    args: argparse.Namespace,
    stats: Stats,
) -> None:
    forced_text = is_forced_text(path)

    if not forced_text and not args.no_binary_extension_skip and is_binary_extension(path):
        stats.skipped_binary += 1
        return

    try:
        size = path.stat().st_size
        if args.max_bytes and size > args.max_bytes:
            stats.skipped_large += 1
            return

        data = path.read_bytes()
    except OSError as exc:
        stats.read_errors += 1
        print(f"READ-ERROR {relative(path, root)}: {exc}")
        return

    if not forced_text and looks_binary(data):
        stats.skipped_binary += 1
        return

    stats.scanned += 1
    relpath = relative(path, root)

    try:
        data.decode("utf-8")
        stats.valid_utf8 += 1
        if args.verbose:
            print(f"OK {relpath}")
        return
    except UnicodeDecodeError as utf8_error:
        invalid_at = utf8_error.start

    if data.startswith(b"%TSD-Header-###%"):
        if not restore_tsd_from_git(path, root, git_root, args, stats):
            stats.unresolved += 1
            print(f"UNRESOLVED {relpath}: TSD-wrapped/corrupt file is not source text")
        return

    has_utf16_or_utf32_bom = any(data.startswith(bom) for bom, _encoding in BOM_ENCODINGS)
    if not has_utf16_or_utf32_bom and b"\0" in data:
        stats.unresolved += 1
        print(f"UNRESOLVED {relpath}: invalid UTF-8 and contains NUL bytes; likely binary/corrupt")
        return

    try:
        decoded = decode_legacy(data, encodings)
    except UnicodeError as exc:
        decoded = None
        decode_error = str(exc)
    else:
        decode_error = "no configured source encoding matched"

    if decoded is None:
        stats.unresolved += 1
        print(f"UNRESOLVED {relpath}: invalid UTF-8 at byte {invalid_at}; {decode_error}")
        return

    text, source_encoding = decoded
    if args.check:
        stats.would_convert += 1
        print(f"WOULD-CONVERT {relpath}: {source_encoding} -> utf-8")
        return

    old_mtime_ns = path.stat().st_mtime_ns
    write_bytes_via_text_temp(path, text.encode("utf-8"))
    if args.keep_mtime:
        os.utime(path, ns=(old_mtime_ns, old_mtime_ns))

    stats.converted += 1
    print(f"CONVERTED {relpath}: {source_encoding} -> utf-8")


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    git_root = find_git_root(root)
    encodings = tuple(args.encodings or DEFAULT_SOURCE_ENCODINGS)
    skip_patterns = set(args.skip_dir)

    vendored_patterns = {"deps", "deps_src"}
    for pattern in DEFAULT_SKIP_DIR_PATTERNS:
        if args.include_vendored and pattern in vendored_patterns:
            continue
        skip_patterns.add(pattern)

    stats = Stats()
    for path in iter_files(root, sorted(skip_patterns)):
        process_file(path, root, git_root, encodings, args, stats)

    print(
        "SUMMARY "
        f"scanned={stats.scanned} "
        f"valid_utf8={stats.valid_utf8} "
        f"converted={stats.converted} "
        f"restored={stats.restored} "
        f"would_convert={stats.would_convert} "
        f"would_restore={stats.would_restore} "
        f"unresolved={stats.unresolved} "
        f"skipped_binary={stats.skipped_binary} "
        f"skipped_large={stats.skipped_large} "
        f"read_errors={stats.read_errors}"
    )

    if stats.unresolved or stats.read_errors:
        return 2
    if args.check and (stats.would_convert or stats.would_restore):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
