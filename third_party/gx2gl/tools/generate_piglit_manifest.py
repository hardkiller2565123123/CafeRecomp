#!/usr/bin/env python3
"""Build the SD-card Piglit corpus layout used by gl33_test.rpx."""

from __future__ import annotations

import argparse
import shutil
from collections import Counter
from pathlib import Path


CASE_SUFFIXES = {
    ".shader_test": "shader_test",
    ".glsl_parser_test": "glsl_parser_test",
    ".c": "native_c",
    ".cpp": "native_cpp",
    ".py": "python",
    ".cl": "opencl",
}


def repo_relative(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def case_kind(path: Path) -> str | None:
    name = path.name
    for suffix, kind in CASE_SUFFIXES.items():
        if name.endswith(suffix):
            return kind
    return None


def iter_cases(piglit_root: Path):
    tests_root = piglit_root / "tests"
    generated_root = piglit_root / "generated_tests"

    if tests_root.exists():
        for path in sorted(tests_root.rglob("*")):
            if not path.is_file():
                continue
            kind = case_kind(path)
            if kind is None:
                continue
            rel = repo_relative(path, piglit_root)
            yield rel.removeprefix("tests/"), kind, rel

    if generated_root.exists():
        for path in sorted(generated_root.rglob("*")):
            if not path.is_file():
                continue
            if path.suffix == ".py":
                kind = "generator_python"
            elif path.name.endswith(".mako"):
                kind = "generator_template"
            else:
                continue
            rel = repo_relative(path, piglit_root)
            yield rel, kind, rel


def write_manifest(
    piglit_root: Path, out_root: Path, copy_sources: bool, shard_size: int
) -> None:
    manifest_path = out_root / "piglit_manifest.tsv"
    source_root = out_root / "piglit"
    shard_root = out_root / "piglit_shards"
    counts: Counter[str] = Counter()
    rows: list[tuple[str, str, str]] = list(iter_cases(piglit_root))

    out_root.mkdir(parents=True, exist_ok=True)
    if copy_sources:
        source_root.mkdir(parents=True, exist_ok=True)

    with manifest_path.open("w", encoding="utf-8", newline="\n") as manifest:
        manifest.write("# name\tkind\tpath\n")
        for name, kind, rel in rows:
            counts[kind] += 1
            manifest.write(f"{name}\t{kind}\t{rel}\n")
            if copy_sources and kind in {"shader_test", "glsl_parser_test"}:
                dst = source_root / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(piglit_root / rel, dst)

    if shard_size > 0:
        if shard_root.exists():
            shutil.rmtree(shard_root)
        shard_root.mkdir(parents=True, exist_ok=True)
        for index in range(0, len(rows), shard_size):
            shard_index = index // shard_size
            shard_path = shard_root / f"piglit_manifest_{shard_index:03d}.tsv"
            with shard_path.open("w", encoding="utf-8", newline="\n") as shard:
                shard.write("# name\tkind\tpath\n")
                for name, kind, rel in rows[index : index + shard_size]:
                    shard.write(f"{name}\t{kind}\t{rel}\n")

    total = sum(counts.values())
    print(f"Wrote {manifest_path}")
    print(f"Total Piglit inventory: {total}")
    for kind, count in sorted(counts.items()):
        print(f"{kind}: {count}")
    if copy_sources:
        print(f"Copied shader sources under {source_root}")
    if shard_size > 0:
        shard_count = (len(rows) + shard_size - 1) // shard_size
        print(f"Wrote {shard_count} shard manifests under {shard_root}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate gx2gl's SD-card Piglit manifest."
    )
    parser.add_argument(
        "--piglit-root",
        required=True,
        type=Path,
        help="Path to an upstream Piglit checkout.",
    )
    parser.add_argument(
        "--out-root",
        default=Path("dist/piglit_sd/gx2gl"),
        type=Path,
        help="Output gx2gl folder to copy to SD/content.",
    )
    parser.add_argument(
        "--manifest-only",
        action="store_true",
        help="Write only piglit_manifest.tsv; do not copy shader sources.",
    )
    parser.add_argument(
        "--shard-size",
        default=0,
        type=int,
        help="Also write piglit_shards/piglit_manifest_NNN.tsv chunks.",
    )
    args = parser.parse_args()

    piglit_root = args.piglit_root.resolve()
    out_root = args.out_root.resolve()
    if not (piglit_root / "tests").exists():
        parser.error(f"{piglit_root} does not look like a Piglit checkout")

    if args.shard_size < 0:
        parser.error("--shard-size must be non-negative")

    write_manifest(piglit_root, out_root, not args.manifest_only, args.shard_size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
