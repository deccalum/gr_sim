from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
from typing import Iterable


COMMENT_BLOCK_RE = re.compile(r"/\*\*(.*?)\*/", re.DOTALL)
ASSIGNMENT_RE = re.compile(r"^\s*([A-Za-z_]\w*(?:\s*(?:\[[^\]]*\]|\.\w+|->\w+))*)\s*([+\-*/%]?=)\s*(.+?);\s*$")
DECLARATION_ASSIGNMENT_RE = re.compile(
  r"^\s*(?:constexpr|consteval|const|volatile|static|inline|extern|thread_local|auto|unsigned|signed|long|short|float|double|bool|char|int|"
  r"std::[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)(?:[\w:\<\>,\s*&]+)?\s+([A-Za-z_]\w*)\s*=\s*(.+?);\s*$"
)
INLINE_COMMENT_RE = re.compile(r"//.*$")
MATH_FUNCTION_RE = re.compile(r"\b(?:sin|cos|tan|sqrt|pow|exp|log|atan2|hypot|abs|fabs)\s*\(", re.IGNORECASE)
BINARY_OPERATOR_RE = re.compile(r"[+\-*/^]")
ALPHANUM_TERM_RE = re.compile(r"[A-Za-z_]\w*")
DECLARATION_PREFIX_RE = re.compile(
  r"^\s*(?:constexpr|consteval|const|volatile|static|inline|extern|thread_local|auto|unsigned|signed|long|short|float|double|bool|char|int|"
  r"std::[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)(?:[\w:\<\>,\s*&]+)?\s+.+;\s*$"
)

DEFAULT_COORD_SYMBOLS = {
  "0": "t",
  "1": "r",
  "2": "theta",
  "3": "phi",
}

DEFAULT_SYMBOL_DESCRIPTIONS = {
  "M_": "Schwarzschild mass parameter",
  "M": "Schwarzschild mass parameter",
  "x[0]": "coordinate time t",
  "x[1]": "radial coordinate r",
  "x[2]": "polar angle theta",
  "x[3]": "azimuthal angle phi",
}


@dataclass(slots=True)
class DoxygenFields:
  brief: str = ""
  details: str = ""
  params: dict[str, str] | None = None
  notes: list[str] | None = None
  math: list[str] | None = None

  def __post_init__(self) -> None:
    if self.params is None:
      self.params = {}
    if self.notes is None:
      self.notes = []
    if self.math is None:
      self.math = []


@dataclass(slots=True)
class ExtractedBlock:
  file_path: str
  section: str
  body: str
  line_no: int | None = None
  kind: str = "comment"


def _normalize_comment_text(raw: str) -> str:
  lines = []
  for line in raw.splitlines():
    cleaned = line.strip()
    if cleaned.startswith("*"):
      cleaned = cleaned[1:].strip()
    lines.append(cleaned)
  return "\n".join(line for line in lines if line)


def _extract_inline_math_tags(text: str) -> list[str]:
  return [match.strip() for match in re.findall(r"@f\$(.+?)@f\$", text)]


def _parse_doxygen_fields(lines: list[str]) -> DoxygenFields:
  fields = DoxygenFields()
  current_tag: tuple[str, str | None] | None = None

  for raw_line in lines:
    line = raw_line.strip()
    if not line:
      continue

    fields.math.extend(_extract_inline_math_tags(line))

    if line.startswith("@brief"):
      fields.brief = line[len("@brief"):].strip()
      current_tag = ("brief", None)
      continue
    if line.startswith("@details"):
      fields.details = line[len("@details"):].strip()
      current_tag = ("details", None)
      continue
    if line.startswith("@note") or line.startswith("@warning"):
      prefix = "@note" if line.startswith("@note") else "@warning"
      value = line[len(prefix):].strip()
      if value:
        fields.notes.append(value)
      current_tag = ("note", None)
      continue
    if line.startswith("@param"):
      param_text = line[len("@param"):].strip()
      match = re.match(r"(?:\[[^\]]+\]\s+)?([A-Za-z_]\w*)\s*(.*)$", param_text)
      if match:
        param_name = match.group(1)
        param_desc = match.group(2).strip()
        if param_name:
          fields.params[param_name] = param_desc
          current_tag = ("param", param_name)
          continue
    if line.startswith("@"):
      current_tag = None
      continue

    if current_tag is None:
      if not fields.brief:
        fields.brief = line
      elif not fields.details:
        fields.details = line
      else:
        fields.details = f"{fields.details} {line}".strip()
      continue

    tag, key = current_tag
    if tag == "brief":
      fields.brief = f"{fields.brief} {line}".strip()
    elif tag == "details":
      fields.details = f"{fields.details} {line}".strip()
    elif tag == "note":
      if fields.notes:
        fields.notes[-1] = f"{fields.notes[-1]} {line}".strip()
      else:
        fields.notes.append(line)
    elif tag == "param" and key:
      prior = fields.params.get(key, "")
      fields.params[key] = f"{prior} {line}".strip() if prior else line

  # Deduplicate extracted inline formulas while preserving order.
  deduped_math: list[str] = []
  seen_math: set[str] = set()
  for entry in fields.math:
    if entry not in seen_math:
      deduped_math.append(entry)
      seen_math.add(entry)
  fields.math = deduped_math
  return fields


def _strip_inline_comment(line: str) -> str:
  return INLINE_COMMENT_RE.sub("", line).rstrip()


def _split_top_level(expr: str, delimiter: str = ",") -> list[str]:
  parts: list[str] = []
  current: list[str] = []
  depth_paren = 0
  depth_bracket = 0
  depth_brace = 0
  for ch in expr:
    if ch == "(":
      depth_paren += 1
    elif ch == ")":
      depth_paren = max(0, depth_paren - 1)
    elif ch == "[":
      depth_bracket += 1
    elif ch == "]":
      depth_bracket = max(0, depth_bracket - 1)
    elif ch == "{":
      depth_brace += 1
    elif ch == "}":
      depth_brace = max(0, depth_brace - 1)

    if ch == delimiter and depth_paren == 0 and depth_bracket == 0 and depth_brace == 0:
      token = "".join(current).strip()
      if token:
        parts.append(token)
      current = []
      continue
    current.append(ch)

  tail = "".join(current).strip()
  if tail:
    parts.append(tail)
  return parts


def _extract_assignments_from_declaration(line: str) -> list[tuple[str, str]]:
  stripped = line.strip()
  if not DECLARATION_PREFIX_RE.match(stripped):
    return []
  without_semicolon = stripped[:-1] if stripped.endswith(";") else stripped
  declarators = _split_top_level(without_semicolon, delimiter=",")
  assignments: list[tuple[str, str]] = []
  for declarator in declarators:
    if "=" not in declarator:
      continue
    lhs, rhs = declarator.split("=", maxsplit=1)
    lhs_tokens = lhs.strip().split()
    if not lhs_tokens:
      continue
    lhs_clean = lhs_tokens[-1]
    rhs_clean = rhs.strip()
    if lhs_clean and rhs_clean:
      assignments.append((lhs_clean, rhs_clean))
  return assignments


def _replace_cpp_math_functions(expr: str) -> str:
  out = expr
  out = re.sub(r"\bstd::", "", out)
  out = re.sub(r"\bpow\s*\(\s*([^,]+?)\s*,\s*2\s*\)", r"(\1)^2", out)
  out = re.sub(r"\bpow\s*\(\s*([^,]+?)\s*,\s*3\s*\)", r"(\1)^3", out)
  return out


def _apply_aliases(expr: str, aliases: dict[str, str]) -> str:
  out = expr
  for alias in sorted(aliases.keys(), key=len, reverse=True):
    out = re.sub(rf"\b{re.escape(alias)}\b", f"({aliases[alias]})", out)
  return out


def _replace_indexed_symbols(expr: str, coord_symbols: dict[str, str]) -> str:
  def coord_to_symbol(value: str) -> str:
    return {
        "theta": "θ",
        "phi": "φ",
    }.get(value, value)

  def repl_coord(match: re.Match[str]) -> str:
    idx = match.group(1)
    return coord_to_symbol(coord_symbols.get(idx, f"x_{idx}"))

  def repl_metric(match: re.Match[str]) -> str:
    i, j = match.group(1), match.group(2)
    return f"g_{{{coord_to_symbol(coord_symbols.get(i, i))}{coord_to_symbol(coord_symbols.get(j, j))}}}"

  def repl_metric_inv(match: re.Match[str]) -> str:
    i, j = match.group(1), match.group(2)
    return f"g^{{{coord_to_symbol(coord_symbols.get(i, i))}{coord_to_symbol(coord_symbols.get(j, j))}}}"

  out = expr
  out = re.sub(r"\bM_\b", "M", out)
  out = re.sub(r"\bx\s*\[\s*(\d+)\s*\]", repl_coord, out)
  out = re.sub(r"\bginv\s*\[\s*(\d+)\s*\]\s*\[\s*(\d+)\s*\]", repl_metric_inv, out)
  out = re.sub(r"\bg\s*\[\s*(\d+)\s*\]\s*\[\s*(\d+)\s*\]", repl_metric, out)
  return out


def _prettify_ops(expr: str) -> str:
  out = expr
  out = re.sub(r"\b1(?:\.0+)?\s*/\s*([A-Za-z][A-Za-z0-9_{}^]*)", r"\1^-1", out)
  out = re.sub(r"\(\s*([A-Za-z][A-Za-z0-9_{}^]*)\s*\)\s*\*\s*\(\s*\1\s*\)", r"\1^2", out)
  out = re.sub(r"\b([A-Za-z][A-Za-z0-9_{}^]*)\s*\*\s*\1\b", r"\1^2", out)
  out = re.sub(r"\s*\*\s*", " · ", out)
  out = re.sub(r"\s*/\s*", "/", out)
  out = re.sub(r"\s*\+\s*", " + ", out)
  out = re.sub(r"\s*-\s*", " - ", out)
  out = re.sub(r"\btheta\b", "θ", out)
  out = re.sub(r"\bphi\b", "φ", out)
  out = re.sub(r"\(\s*sin\(([^)]+)\)\s*\)\s*·\s*\(\s*sin\(\1\)\s*\)", r"sin^2(\1)", out)
  out = re.sub(r"\bsin\(\s*([^)]+)\s*\)\s*\^\s*2\b", r"sin^2(\1)", out)
  out = re.sub(r"(\d(?:\.\d+)?)e\s*-\s*(\d+)", r"\1e-\2", out)
  out = re.sub(r"\s+", " ", out).strip()
  return out


def _to_readable_formula(lhs: str, rhs: str, aliases: dict[str, str], coord_symbols: dict[str, str]) -> str:
  lhs_out = _replace_indexed_symbols(lhs, coord_symbols)
  rhs_out = _replace_cpp_math_functions(rhs)
  rhs_out = _replace_indexed_symbols(rhs_out, coord_symbols)
  rhs_out = _apply_aliases(rhs_out, aliases)
  rhs_out = _prettify_ops(rhs_out)
  lhs_out = _prettify_ops(lhs_out)
  return f"{lhs_out} = {rhs_out}"


def _collect_symbol_notes(
    formula: str,
    symbol_descriptions: dict[str, str],
) -> list[str]:
  notes: list[str] = []
  for symbol, description in symbol_descriptions.items():
    if symbol in formula:
      notes.append(f"{symbol}: {description}")
  deduped: list[str] = []
  seen: set[str] = set()
  for note in notes:
    if note in seen:
      continue
    deduped.append(note)
    seen.add(note)
  return deduped


def _is_formula_candidate(lhs: str, expression: str, markers: set[str]) -> bool:
  cleaned = expression.strip()
  if not cleaned:
    return False

  lowered_lhs = lhs.lower()
  if lowered_lhs.startswith("g[") or lowered_lhs.startswith("ginv["):
    return True

  has_math_operator = bool(re.search(r"[+\-*/^()]", cleaned))
  has_math_function = bool(MATH_FUNCTION_RE.search(cleaned))
  if not (has_math_operator or has_math_function):
    return False

  lowered = cleaned.lower()
  has_marker = any(marker in lowered for marker in markers)
  if has_marker:
    return True

  # Accept function-based expressions even without explicit marker terms.
  if has_math_function:
    return True

  # Accept operator expressions with meaningful symbolic terms.
  rhs = cleaned.split("=", maxsplit=1)[-1]
  has_binary_op = bool(BINARY_OPERATOR_RE.search(rhs))
  symbolic_terms = ALPHANUM_TERM_RE.findall(rhs)
  return has_binary_op and len(symbolic_terms) >= 2


def _collect_related_comments(lines: list[str], line_index: int, max_lookback: int) -> str:
  comments: list[str] = []
  captured_block_comment = False
  scanned = 0
  cursor = line_index - 1

  while cursor >= 0 and scanned < max_lookback:
    line = lines[cursor].strip()
    if not line:
      scanned += 1
      cursor -= 1
      continue
    if line.endswith("*/"):
      block_lines: list[str] = []
      while cursor >= 0:
        block_line = lines[cursor].strip()
        block_line = block_line.rstrip()
        if block_line.endswith("*/"):
          block_line = block_line[:-2].strip()
        if block_line.startswith("/*"):
          block_line = block_line[2:].strip()
          block_line = block_line.lstrip("*").strip()
          if block_line:
            block_lines.append(block_line)
          break
        block_line = block_line.lstrip("*").strip()
        if block_line:
          block_lines.append(block_line)
        cursor -= 1

      block_lines.reverse()
      comments.extend([entry for entry in block_lines if entry])
      captured_block_comment = True
      break
    if line.startswith("//"):
      comments.append(line[2:].strip())
      scanned += 1
      cursor -= 1
      continue
    break

  if not captured_block_comment:
    comments.reverse()
  return "\n".join(comment for comment in comments if comment)


def _collect_related_doxygen_fields(lines: list[str], line_index: int, max_lookback: int) -> DoxygenFields:
  cursor = line_index - 1
  scanned = 0
  window = max(max_lookback, 80)

  while cursor >= 0 and scanned < window:
    line = lines[cursor].strip()
    if not line:
      scanned += 1
      cursor -= 1
      continue
    if line.endswith("*/"):
      block_lines: list[str] = []
      while cursor >= 0:
        block_line = lines[cursor].strip().rstrip()
        if block_line.endswith("*/"):
          block_line = block_line[:-2].strip()
        if block_line.startswith("/*"):
          block_line = block_line[2:].strip()
          block_line = block_line.lstrip("*").strip()
          if block_line:
            block_lines.append(block_line)
          break
        block_line = block_line.lstrip("*").strip()
        if block_line:
          block_lines.append(block_line)
        cursor -= 1

      block_lines.reverse()
      if block_lines:
        return _parse_doxygen_fields(block_lines)
      break
    scanned += 1
    cursor -= 1
    continue

  return DoxygenFields()


def extract_blocks_from_file(
    path: Path,
    *,
    math_markers: Iterable[str] | None = None,
    include_comment_blocks: bool = True,
    max_comment_lookback: int = 4,
    coord_symbols: dict[str, str] | None = None,
    symbol_descriptions: dict[str, str] | None = None,
) -> list[ExtractedBlock]:
  text = path.read_text(encoding="utf-8", errors="ignore")
  lines = text.splitlines()
  blocks: list[ExtractedBlock] = []
  local_aliases: dict[str, str] = {}
  braces = 0
  marker_set = {
      marker.lower()
      for marker in (math_markers or ["gamma", "christoffel", "metric", "g_", "ds^2", "curvature"])
  }
  coord_symbol_map = dict(DEFAULT_COORD_SYMBOLS)
  if coord_symbols:
    coord_symbol_map.update({str(key): value for key, value in coord_symbols.items()})

  symbol_description_map = dict(DEFAULT_SYMBOL_DESCRIPTIONS)
  if symbol_descriptions:
    symbol_description_map.update(symbol_descriptions)

  if include_comment_blocks:
    for idx, match in enumerate(COMMENT_BLOCK_RE.finditer(text), start=1):
      normalized = _normalize_comment_text(match.group(1))
      if normalized:
        blocks.append(
            ExtractedBlock(
                file_path=str(path),
                section=f"comment_{idx}",
                body=normalized,
                kind="comment",
            )
        )

  formula_index = 0
  for line_no, raw_line in enumerate(lines, start=1):
    braces += raw_line.count("{") - raw_line.count("}")
    if braces <= 0:
      local_aliases.clear()

    line = _strip_inline_comment(raw_line)
    if not line or line.lstrip().startswith(("#", "return ", "if ", "for ", "while ", "switch ")):
      continue

    extracted_pairs = _extract_assignments_from_declaration(line)
    if not extracted_pairs:
      assign_match = ASSIGNMENT_RE.match(line)
      if not assign_match:
        continue
      lhs, op, rhs = assign_match.group(1), assign_match.group(2), assign_match.group(3)
      extracted_pairs = [(lhs.strip(), f"{rhs.strip()}" if op == "=" else f"{lhs.strip()} {op[:-1]} {rhs.strip()}")]

    if not extracted_pairs:
      continue

    for lhs, rhs in extracted_pairs:
      expression = f"{lhs} = {rhs}"

      if expression.count("(") != expression.count(")"):
        continue

      if "=" in lhs or "," in lhs:
        continue

      if not _is_formula_candidate(lhs, expression, marker_set):
        if re.match(r"^[A-Za-z_]\w*$", lhs):
          local_aliases[lhs] = _to_readable_formula(lhs, rhs, local_aliases, coord_symbol_map).split("=", maxsplit=1)[-1].strip()
        continue

      formula_index += 1
      related_comment = _collect_related_comments(lines, line_no - 1, max_lookback=max_comment_lookback)
      doc_fields = _collect_related_doxygen_fields(lines, line_no - 1, max_lookback=max_comment_lookback)
      readable = _to_readable_formula(lhs, rhs, local_aliases, coord_symbol_map)
      notes = _collect_symbol_notes(readable, symbol_description_map)

      body_lines = ["Readable:", f"$$ {readable} $$", ""]
      if doc_fields.brief or doc_fields.details or doc_fields.params or doc_fields.notes or doc_fields.math:
        body_lines.append("Doc fields:")
        if doc_fields.brief:
          body_lines.append(f"- brief: {doc_fields.brief}")
        if doc_fields.details:
          body_lines.append(f"- details: {doc_fields.details}")
        for param_name, param_desc in doc_fields.params.items():
          value = param_desc if param_desc else "(no extra detail)"
          body_lines.append(f"- param {param_name}: {value}")
        for note in doc_fields.notes:
          body_lines.append(f"- note: {note}")
        for math_expr in doc_fields.math:
          body_lines.append(f"- math: $$ {math_expr} $$")
        body_lines.append("")
      if notes:
        body_lines.append("Definitions:")
        body_lines.extend(f"- {note}" for note in notes)
        body_lines.append("")
      if related_comment:
        body_lines.extend(["Context:", related_comment, ""])
      body_lines.extend(["Source expression:", "```cpp", f"{expression};", "```"])
      blocks.append(
          ExtractedBlock(
              file_path=str(path),
              section=f"formula_{formula_index}",
              body="\n".join(body_lines),
              line_no=line_no,
              kind="formula",
          )
      )

      if re.match(r"^[A-Za-z_]\w*$", lhs):
        local_aliases[lhs] = readable.split("=", maxsplit=1)[-1].strip()

  return blocks


def collect_cpp_files(root_paths: Iterable[Path], extensions: set[str]) -> list[Path]:
  files: list[Path] = []
  for root in root_paths:
    if not root.exists():
      continue
    for path in root.rglob("*"):
      if path.suffix in extensions and path.is_file():
        files.append(path)
  return sorted(files)
