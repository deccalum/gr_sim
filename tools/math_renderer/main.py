from __future__ import annotations

import argparse
from pathlib import Path

import yaml

from .cpp_extractor import collect_cpp_files, extract_blocks_from_file
from .markdown_renderer import render_markdown
from .math_annotator import annotate_math


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description="Extract math-heavy C++ comments into markdown docs.")
  parser.add_argument("--src", default="src", help="Source directory root.")
  parser.add_argument("--output", default="docs/generated", help="Output directory.")
  parser.add_argument("--config", default="tools/math_renderer/config.yaml", help="Renderer config path.")
  return parser.parse_args()


def load_config(path: Path) -> dict:
  if not path.exists():
    return {}
  return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def main() -> int:
  args = parse_args()
  config = load_config(Path(args.config))

  source_roots = config.get("include_paths") or config.get("source_roots", [args.src])
  root_paths = [Path(p) for p in source_roots]
  extensions = set(config.get("file_extensions", [".h", ".hpp", ".cpp", ".cu"]))
  math_markers = config.get("math_markers", ["gamma", "christoffel", "metric", "g_", "ds^2", "curvature"])
  include_comment_blocks = bool(config.get("include_comment_blocks", False))
  max_comment_lookback = int(config.get("max_comment_lookback", 4))
  coord_symbols = config.get("coord_symbols", {})
  symbol_descriptions = config.get("symbol_descriptions", {})

  files = collect_cpp_files(root_paths=root_paths, extensions=extensions)
  blocks = []
  for path in files:
    for block in extract_blocks_from_file(
        path,
        math_markers=math_markers,
        include_comment_blocks=include_comment_blocks,
        max_comment_lookback=max_comment_lookback,
      coord_symbols=coord_symbols,
      symbol_descriptions=symbol_descriptions,
    ):
      block.body = annotate_math(block.body)
      blocks.append(block)

  output_dir = Path(args.output)
  output_name = config.get("output_file", "physics_reference.md")
  render_markdown(blocks, output_dir / output_name)
  print(f"Generated {len(blocks)} blocks from {len(files)} files.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
