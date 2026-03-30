from __future__ import annotations

from pathlib import Path
from typing import Iterable

from .cpp_extractor import ExtractedBlock


def render_markdown(blocks: Iterable[ExtractedBlock], output_path: Path) -> None:
  output_path.parent.mkdir(parents=True, exist_ok=True)
  ordered_blocks = sorted(
      blocks,
      key=lambda block: (
          block.file_path,
          block.line_no if block.line_no is not None else -1,
          block.section,
      ),
  )
  lines = [
      "# Physics Reference (Generated)",
      "",
      "Generated from C++ formula-like expressions with nearby comment context.",
      "",
      "Math rendering hint: enable MathJax/KaTeX in your markdown viewer.",
      "",
  ]

  current_file = None
  for block in ordered_blocks:
    normalized_path = block.file_path.replace("\\", "/")
    if normalized_path != current_file:
      current_file = normalized_path
      lines.append(f"## {normalized_path}")
      lines.append("")

    lines.append(f"### {block.kind}: {block.section}")
    lines.append("")
    if block.line_no is not None:
      lines.append(f"Line: {block.line_no}")
      lines.append("")
    lines.append("")
    lines.append(block.body)
    lines.append("")

  output_path.write_text("\n".join(lines), encoding="utf-8")
