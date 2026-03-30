from __future__ import annotations

import re


INLINE_MATH_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"ds\^2"), "$ds^2$"),
    (re.compile(r"\bg_([a-zA-Z0-9\\u03bc\\u03bd\\u03bb]+)\b"), r"$g_{\1}$"),
    (re.compile(r"\bGamma\^([a-zA-Z0-9\\u03bb]+)_([a-zA-Z0-9\\u03bc\\u03bd]+)\b"), r"$\\Gamma^{\1}_{\2}$"),
]


def annotate_math(text: str) -> str:
  lines = text.splitlines()
  in_code_block = False
  output_lines: list[str] = []

  for line in lines:
    stripped = line.strip()
    if stripped.startswith("```"):
      in_code_block = not in_code_block
      output_lines.append(line)
      continue

    if in_code_block:
      output_lines.append(line)
      continue

    transformed = line
    for pattern, repl in INLINE_MATH_PATTERNS:
      transformed = pattern.sub(repl, transformed)
    output_lines.append(transformed)

  return "\n".join(output_lines)
