#!/usr/bin/env python3
"""noserde tagged-struct generator.

Input:  C++ source containing [[noserde]] struct blocks.
Output: transformed C++ source with generated wrappers replacing tagged blocks.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import pathlib
import re
import sys
from typing import Iterable, List, Sequence, Tuple

GENERATOR_VERSION = "0.1.8"
FORMAT_VERSION = "1"
ATTRIBUTE_TOKEN = "[[noserde]]"
DIGEST_PATTERN = re.compile(r"^// digest: ([0-9a-f]{64})$", re.MULTILINE)


class ParseError(RuntimeError):
    def __init__(self, message: str, index: int) -> None:
        super().__init__(message)
        self.index = index


@dataclasses.dataclass
class UnionAlt:
    type_name: str
    name: str
    is_record: bool = False
    inline_type_name: str = ""
    inline_record: "InlineRecord | None" = None


@dataclasses.dataclass
class Field:
    kind: str  # scalar | record | union
    name: str
    type_name: str = ""
    union_type_name: str = ""
    union_alts: List[UnionAlt] = dataclasses.field(default_factory=list)
    inline_record: "InlineRecord | None" = None


@dataclasses.dataclass
class InlineRecord:
    name: str
    fields: List[Field] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class StructBlock:
    name: str
    body: str
    start: int
    end: int
    fields: List[Field] = dataclasses.field(default_factory=list)
    helpers: List["StructBlock"] = dataclasses.field(default_factory=list)


def line_col(text: str, index: int) -> Tuple[int, int]:
    line = text.count("\n", 0, index) + 1
    line_start = text.rfind("\n", 0, index)
    if line_start < 0:
        line_start = -1
    col = index - line_start
    return line, col


def fail(path: pathlib.Path, text: str, error: ParseError) -> None:
    line, col = line_col(text, error.index)
    print(f"{path}:{line}:{col}: error: {error}", file=sys.stderr)


def skip_ws_comments(text: str, i: int) -> int:
    n = len(text)
    while i < n:
        ch = text[i]
        if ch.isspace():
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i + 2)
            if j == -1:
                return n
            i = j + 1
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j == -1:
                raise ParseError("unterminated block comment", i)
            i = j + 2
            continue
        return i
    return i


def parse_identifier(text: str, i: int) -> Tuple[str, int]:
    m = re.match(r"[A-Za-z_]\w*", text[i:])
    if not m:
        raise ParseError("expected identifier", i)
    ident = m.group(0)
    return ident, i + len(ident)


def find_attribute_positions(text: str) -> List[int]:
    positions: List[int] = []
    i = 0
    n = len(text)
    mode = "code"
    escape = False

    while i < n:
        if mode == "code":
            if text.startswith("//", i):
                mode = "line"
                i += 2
                continue
            if text.startswith("/*", i):
                mode = "block"
                i += 2
                continue
            if text[i] == '"':
                mode = "dquote"
                escape = False
                i += 1
                continue
            if text[i] == "'":
                mode = "squote"
                escape = False
                i += 1
                continue
            if text.startswith(ATTRIBUTE_TOKEN, i):
                positions.append(i)
                i += len(ATTRIBUTE_TOKEN)
                continue
            i += 1
            continue

        if mode == "line":
            if text[i] == "\n":
                mode = "code"
            i += 1
            continue

        if mode == "block":
            if text.startswith("*/", i):
                mode = "code"
                i += 2
            else:
                i += 1
            continue

        if mode in ("dquote", "squote"):
            quote = '"' if mode == "dquote" else "'"
            ch = text[i]
            if escape:
                escape = False
                i += 1
                continue
            if ch == "\\":
                escape = True
                i += 1
                continue
            if ch == quote:
                mode = "code"
            i += 1
            continue

    return positions


def find_matching_brace(text: str, open_index: int) -> int:
    if open_index >= len(text) or text[open_index] != "{":
        raise ParseError("internal error: expected opening brace", open_index)

    i = open_index
    n = len(text)
    depth = 0
    mode = "code"
    escape = False

    while i < n:
        if mode == "code":
            if text.startswith("//", i):
                mode = "line"
                i += 2
                continue
            if text.startswith("/*", i):
                mode = "block"
                i += 2
                continue
            if text[i] == '"':
                mode = "dquote"
                escape = False
                i += 1
                continue
            if text[i] == "'":
                mode = "squote"
                escape = False
                i += 1
                continue
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    return i
            i += 1
            continue

        if mode == "line":
            if text[i] == "\n":
                mode = "code"
            i += 1
            continue

        if mode == "block":
            if text.startswith("*/", i):
                mode = "code"
                i += 2
            else:
                i += 1
            continue

        if mode in ("dquote", "squote"):
            quote = '"' if mode == "dquote" else "'"
            ch = text[i]
            if escape:
                escape = False
                i += 1
                continue
            if ch == "\\":
                escape = True
                i += 1
                continue
            if ch == quote:
                mode = "code"
            i += 1
            continue

    raise ParseError("unbalanced braces", open_index)


def split_top_level_decls(body: str) -> List[str]:
    decls: List[str] = []
    start = 0
    i = 0
    n = len(body)
    depth = 0
    mode = "code"
    escape = False

    while i < n:
        if mode == "code":
            if body.startswith("//", i):
                mode = "line"
                i += 2
                continue
            if body.startswith("/*", i):
                mode = "block"
                i += 2
                continue
            ch = body[i]
            if ch == '"':
                mode = "dquote"
                escape = False
                i += 1
                continue
            if ch == "'":
                mode = "squote"
                escape = False
                i += 1
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth < 0:
                    raise ParseError("unexpected closing brace", i)
            elif ch == ";" and depth == 0:
                decl = body[start:i].strip()
                if decl:
                    decls.append(decl)
                start = i + 1
            i += 1
            continue

        if mode == "line":
            if body[i] == "\n":
                mode = "code"
            i += 1
            continue

        if mode == "block":
            if body.startswith("*/", i):
                mode = "code"
                i += 2
            else:
                i += 1
            continue

        if mode in ("dquote", "squote"):
            quote = '"' if mode == "dquote" else "'"
            ch = body[i]
            if escape:
                escape = False
                i += 1
                continue
            if ch == "\\":
                escape = True
                i += 1
                continue
            if ch == quote:
                mode = "code"
            i += 1
            continue

    trailing = body[start:].strip()
    if trailing:
        raise ParseError("expected ';' after declaration", start)

    return decls


def normalize_type(type_name: str) -> str:
    return " ".join(type_name.strip().split())


def parse_type_name_pair(decl: str, origin_index: int = 0) -> Tuple[str, str]:
    if "=" in decl:
        raise ParseError("default field initializers are not supported", origin_index + decl.index("="))

    match = re.match(r"^(?P<type>.+?)\s+(?P<name>[A-Za-z_]\w*)$", decl.strip(), re.DOTALL)
    if not match:
        raise ParseError("expected '<type> <name>' declaration", origin_index)

    type_name = normalize_type(match.group("type"))
    name = match.group("name")

    if any(token in type_name for token in ("*", "&")):
        raise ParseError("pointers/references are not supported", origin_index)
    if "[" in decl or "]" in decl:
        raise ParseError("arrays are not supported", origin_index)
    if not type_name.strip().startswith("struct") and re.search(
        r"\b(bitfield|template|class\s+|union\s+)", type_name
    ):
        raise ParseError("inline aggregate/type declarations are not supported", origin_index)

    return type_name, name


def parse_inline_struct(type_name: str, origin_index: int) -> Tuple[str, str] | None:
    stripped = type_name.strip()
    if not stripped.startswith("struct"):
        return None

    rest = stripped[len("struct") :].lstrip()
    if rest.startswith("{"):
        raise ParseError(
            "anonymous nested structs are not supported; use 'struct Name { ... } member;'",
            origin_index,
        )

    record_match = re.match(r"^(?P<name>[A-Za-z_]\w*)", rest)
    if not record_match:
        raise ParseError("expected nested struct name after 'struct'", origin_index)

    record_name = record_match.group("name")
    after_name = rest[record_match.end() :].lstrip()
    if not after_name.startswith("{"):
        raise ParseError(
            "named 'struct T' type spellings are not supported; use plain type names",
            origin_index,
        )

    close = find_matching_brace(after_name, 0)
    trailing = after_name[close + 1 :].strip()
    if trailing:
        raise ParseError("inline struct field type contains unexpected trailing tokens", origin_index)

    return record_name, after_name[1:close]


def parse_union_decl(decl: str, origin_index: int) -> Field:
    union_kw = re.match(r"^union\b", decl)
    if not union_kw:
        raise ParseError("internal error: union declaration expected", origin_index)

    open_brace = decl.find("{")
    if open_brace == -1:
        raise ParseError("expected '{' in union declaration", origin_index)

    union_head = normalize_type(decl[:open_brace].strip())
    union_head_match = re.match(r"^union\s+([A-Za-z_]\w*)$", union_head)
    if not union_head_match:
        if union_head == "union":
            raise ParseError(
                "anonymous unions are not supported; use 'union Name { ... } field;'",
                origin_index,
            )
        raise ParseError(
            "expected named union declaration: 'union Name { ... } field;'",
            origin_index,
        )
    union_type_name = union_head_match.group(1)

    close_brace = find_matching_brace(decl, open_brace)
    after = decl[close_brace + 1 :].strip()
    if not re.match(r"^[A-Za-z_]\w*$", after):
        raise ParseError("expected union field name after '}'", origin_index + close_brace + 1)

    field_name = after
    inner = decl[open_brace + 1 : close_brace]
    alt_decls = split_top_level_decls(inner)
    if not alt_decls:
        raise ParseError("union must contain at least one alternative", origin_index + open_brace)

    alts: List[UnionAlt] = []
    inline_type_names: set[str] = set()
    for alt_decl in alt_decls:
        alt_type, alt_name = parse_type_name_pair(alt_decl, origin_index)
        inline_struct = parse_inline_struct(alt_type, origin_index)
        if inline_struct is None:
            alts.append(UnionAlt(type_name=alt_type, name=alt_name))
        else:
            inline_name, inline_body = inline_struct
            if inline_name in inline_type_names:
                raise ParseError(
                    f"duplicate inline type name '{inline_name}' in union '{union_type_name}'",
                    origin_index,
                )
            inline_type_names.add(inline_name)
            inline_fields = parse_struct_fields(inline_body, origin_index)
            alts.append(
                UnionAlt(
                    type_name="",
                    name=alt_name,
                    is_record=True,
                    inline_type_name=inline_name,
                    inline_record=InlineRecord(name=inline_name, fields=inline_fields),
                )
            )

    return Field(kind="union", name=field_name, union_type_name=union_type_name, union_alts=alts)


def parse_struct_fields(body: str, body_origin_index: int) -> List[Field]:
    fields: List[Field] = []
    decls = split_top_level_decls(body)

    for decl in decls:
        stripped = decl.strip()
        if not stripped:
            continue
        if stripped in ("public:", "private:", "protected:"):
            continue

        if re.match(r"^union\b", stripped):
            fields.append(parse_union_decl(stripped, body_origin_index + body.find(stripped)))
            continue

        type_name, field_name = parse_type_name_pair(stripped, body_origin_index + body.find(stripped))
        inline_struct = parse_inline_struct(type_name, body_origin_index + body.find(stripped))
        if inline_struct is None:
            fields.append(Field(kind="scalar", name=field_name, type_name=type_name))
        else:
            inline_name, inline_body = inline_struct
            inline_fields = parse_struct_fields(inline_body, body_origin_index + body.find(stripped))
            fields.append(
                Field(
                    kind="record",
                    name=field_name,
                    type_name="",
                    inline_record=InlineRecord(name=inline_name, fields=inline_fields),
                )
            )

    return fields


def parse_tagged_struct(text: str, attr_index: int) -> StructBlock:
    i = attr_index + len(ATTRIBUTE_TOKEN)
    i = skip_ws_comments(text, i)

    if not text.startswith("struct", i):
        raise ParseError("expected 'struct' after [[noserde]]", i)
    end_struct_kw = i + len("struct")
    if end_struct_kw < len(text) and (text[end_struct_kw].isalnum() or text[end_struct_kw] == "_"):
        raise ParseError("expected 'struct' keyword", i)

    i = skip_ws_comments(text, end_struct_kw)
    struct_name, i = parse_identifier(text, i)

    i = skip_ws_comments(text, i)
    if i >= len(text) or text[i] != "{":
        raise ParseError("expected '{' to open struct body", i)

    open_brace = i
    close_brace = find_matching_brace(text, open_brace)
    body = text[open_brace + 1 : close_brace]

    i = skip_ws_comments(text, close_brace + 1)
    if i >= len(text) or text[i] != ";":
        raise ParseError("expected ';' after struct declaration", i)

    end = i + 1
    return StructBlock(name=struct_name, body=body, start=attr_index, end=end)


def parse_all_structs(text: str) -> List[StructBlock]:
    positions = find_attribute_positions(text)
    blocks: List[StructBlock] = []
    consumed_until = -1

    for pos in positions:
        if pos < consumed_until:
            continue
        block = parse_tagged_struct(text, pos)
        blocks.append(block)
        consumed_until = block.end

    return blocks


def synthesize_inline_helpers_for_block(block: StructBlock) -> List[StructBlock]:
    helpers: List[StructBlock] = []
    used_names: set[str] = set()

    def reserve_helper_name(*parts: str) -> str:
        safe_parts = [p for p in parts if p]
        base = "__".join(safe_parts)
        candidate = base
        suffix = 2
        while candidate in used_names:
            candidate = f"{base}_{suffix}"
            suffix += 1
        used_names.add(candidate)
        return candidate

    def synthesize_from_fields(fields: Sequence[Field], path: Sequence[str]) -> None:
        for field in fields:
            if field.kind == "union":
                for alt in field.union_alts:
                    if alt.inline_record is None:
                        continue
                    inline_name = alt.inline_record.name
                    union_path_name = field.union_type_name if field.union_type_name else field.name
                    helper_name = reserve_helper_name(*(list(path) + [union_path_name, alt.name, inline_name]))
                    synthesize_from_fields(
                        alt.inline_record.fields,
                        list(path) + [union_path_name, alt.name, inline_name],
                    )
                    helpers.append(
                        StructBlock(
                            name=helper_name,
                            body="",
                            start=block.start,
                            end=block.end,
                            fields=alt.inline_record.fields,
                        )
                    )
                    alt.type_name = helper_name
                    alt.is_record = True
                    alt.inline_record = None
            else:
                if field.inline_record is None:
                    continue
                inline_name = field.inline_record.name
                helper_name = reserve_helper_name(*(list(path) + [field.name, inline_name]))
                synthesize_from_fields(field.inline_record.fields, list(path) + [field.name, inline_name])
                helpers.append(
                    StructBlock(
                        name=helper_name,
                        body="",
                        start=block.start,
                        end=block.end,
                        fields=field.inline_record.fields,
                    )
                )
                field.type_name = helper_name
                field.kind = "record"
                field.inline_record = None

    synthesize_from_fields(block.fields, [block.name])
    return helpers


def classify_field_list(fields: Sequence[Field], record_names: set[str]) -> None:
    for field in fields:
        if field.kind == "union":
            for alt in field.union_alts:
                alt.is_record = alt.type_name in record_names
        else:
            field.kind = "record" if field.type_name in record_names else "scalar"


def classify_fields(blocks: Sequence[StructBlock]) -> None:
    for block in blocks:
        block.helpers = synthesize_inline_helpers_for_block(block)

    record_names = {b.name for b in blocks}
    for block in blocks:
        for helper in block.helpers:
            record_names.add(helper.name)

    for block in blocks:
        for helper in block.helpers:
            classify_field_list(helper.fields, record_names)
        classify_field_list(block.fields, record_names)


def field_size_expr(type_name: str, is_record: bool) -> str:
    if is_record:
        return f"noserde::record_sizeof<{type_name}>()"
    return f"noserde::wire_sizeof<{type_name}>()"


def data_type_expr_for_alt(alt: UnionAlt) -> str:
    if alt.is_record:
        return f"{alt.type_name}::Data"
    return alt.type_name


def data_type_expr_for_field(field: Field) -> str:
    if field.kind == "record":
        return f"{field.type_name}::Data"
    if field.kind == "union":
        alt_types = ", ".join(data_type_expr_for_alt(alt) for alt in field.union_alts)
        return f"std::variant<{alt_types}>"
    return field.type_name


def schema_hash64(block: StructBlock) -> int:
    parts: List[str] = [block.name]
    for field in block.fields:
        if field.kind == "union":
            parts.append(f"union:{field.name}")
            for alt in field.union_alts:
                parts.append(f"alt:{alt.type_name}:{alt.name}:{int(alt.is_record)}")
        else:
            parts.append(f"{field.kind}:{field.type_name}:{field.name}")

    digest = hashlib.sha256("\n".join(parts).encode("utf-8")).digest()
    return int.from_bytes(digest[:8], byteorder="little", signed=False)


def render_union_class(union_field: Field, const_ref: bool) -> List[str]:
    class_name = f"{union_field.name}_union_const_ref" if const_ref else f"{union_field.name}_union_ref"
    tag_ptr_type = "const std::byte*" if const_ref else "std::byte*"
    payload_ptr_type = "const std::byte*" if const_ref else "std::byte*"

    lines: List[str] = []
    lines.append(f"  class {class_name} {{")
    lines.append("   private:")
    lines.append(f"    {tag_ptr_type} tag_ptr_;")
    lines.append(f"    {payload_ptr_type} payload_ptr_;")
    lines.append("")
    lines.append("   public:")

    for alt in union_field.union_alts:
        if alt.is_record:
            member_type = (
                f"typename noserde::record_traits<{alt.type_name}>::const_ref"
                if const_ref
                else f"typename noserde::record_traits<{alt.type_name}>::ref"
            )
        else:
            member_type = (
                f"noserde::scalar_cref<{alt.type_name}>"
                if const_ref
                else f"noserde::scalar_ref<{alt.type_name}>"
            )
        lines.append(f"    {member_type} {alt.name};")

    lines.append("")
    init_list = ["tag_ptr_(tag_ptr)", "payload_ptr_(payload_ptr)"]
    for alt in union_field.union_alts:
        if alt.is_record:
            if const_ref:
                init_list.append(
                    f"{alt.name}(noserde::make_record_const_ref<{alt.type_name}>(payload_ptr))"
                )
            else:
                init_list.append(f"{alt.name}(noserde::make_record_ref<{alt.type_name}>(payload_ptr))")
        else:
            init_list.append(f"{alt.name}(payload_ptr)")

    lines.append(
        f"    explicit {class_name}({tag_ptr_type} tag_ptr, {payload_ptr_type} payload_ptr)"
    )
    if init_list:
        lines.append("        : " + ",\n          ".join(init_list) + " {}")
    else:
        lines.append("        {}")

    lines.append("")
    lines.append("    std::size_t index() const {")
    lines.append("      return static_cast<std::size_t>(noserde::load_le<std::uint32_t>(tag_ptr_));")
    lines.append("    }")
    lines.append("")
    lines.append("    template <typename Alternative>")
    lines.append("    static consteval std::size_t type_count() {")
    count_terms = " + ".join(
        f"(std::is_same_v<Alternative, {alt.type_name}> ? 1u : 0u)" for alt in union_field.union_alts
    )
    lines.append(f"      return {count_terms};")
    lines.append("    }")

    lines.append("")
    lines.append("    template <typename Alternative>")
    lines.append("    static consteval std::size_t type_index() {")
    lines.append(
        "      static_assert(type_count<Alternative>() == 1u, \"alternative type must appear exactly once in this union\");"
    )
    for idx, alt in enumerate(union_field.union_alts):
        cond = "if" if idx == 0 else "else if"
        lines.append(f"      {cond} constexpr (std::is_same_v<Alternative, {alt.type_name}>) {{")
        lines.append(f"        return {idx};")
        lines.append("      }")
    lines.append("      else {")
    lines.append("        static_assert(noserde::always_false_v<Alternative>, \"unknown union alternative type\");")
    lines.append("      }")
    lines.append("    }")

    lines.append("")
    lines.append("    template <typename Alternative>")
    lines.append("    bool holds_alternative() const {")
    lines.append("      return index() == type_index<Alternative>();")
    lines.append("    }")

    lines.append("")
    lines.append("    template <typename Alternative>")
    if const_ref:
        lines.append("    auto get_if() const {")
    else:
        lines.append("    auto get_if() {")
    for idx, alt in enumerate(union_field.union_alts):
        cond = "if" if idx == 0 else "else if"
        lines.append(f"      {cond} constexpr (std::is_same_v<Alternative, {alt.type_name}>) {{")
        lines.append("        if (!holds_alternative<Alternative>()) {")
        lines.append(f"          return static_cast<decltype(&{alt.name})>(nullptr);")
        lines.append("        }")
        lines.append(f"        return &{alt.name};")
        lines.append("      }")
    lines.append("      else {")
    lines.append("        static_assert(noserde::always_false_v<Alternative>, \"unknown union alternative type\");")
    lines.append("      }")
    lines.append("    }")

    lines.append("")
    lines.append("    template <typename Visitor>")
    visit_sig = "decltype(auto) visit(Visitor&& visitor) const" if const_ref else "decltype(auto) visit(Visitor&& visitor)"
    lines.append(f"    {visit_sig} {{")
    lines.append("      switch (index()) {")
    for idx, alt in enumerate(union_field.union_alts):
        lines.append(f"        case {idx}:")
        lines.append(f"          return std::forward<Visitor>(visitor)({alt.name});")
    lines.append("        default:")
    lines.append("          std::abort();")
    lines.append("      }")
    lines.append("    }")

    if not const_ref:
        lines.append("")
        lines.append("    template <typename Alternative, typename... Args>")
        lines.append("    void emplace(Args&&... args) {")
        lines.append("      noserde::zero_bytes(payload_ptr_, __layout::" + union_field.name + "_payload_size);")

        for idx, alt in enumerate(union_field.union_alts):
            cond = "if" if idx == 0 else "else if"
            lines.append(f"      {cond} constexpr (std::is_same_v<Alternative, {alt.type_name}>) {{")
            lines.append(
                f"        noserde::store_le<std::uint32_t>(tag_ptr_, static_cast<std::uint32_t>({idx}));"
            )
            if alt.is_record:
                lines.append("        static_assert(sizeof...(Args) == 0, \"record alternatives support only default emplace() in v1\");")
            else:
                lines.append("        static_assert(sizeof...(Args) <= 1, \"emplace supports at most one argument\");")
                lines.append("        if constexpr (sizeof...(Args) == 0) {")
                lines.append(f"          {alt.name} = {alt.type_name}{{}};")
                lines.append("        } else {")
                lines.append(
                    f"          {alt.name} = static_cast<{alt.type_name}>((std::forward<Args>(args), ...));"
                )
                lines.append("        }")
            lines.append("      }")

        lines.append("      else {")
        lines.append("        static_assert(noserde::always_false_v<Alternative>, \"unknown union alternative type\");")
        lines.append("      }")
        lines.append("    }")

    lines.append("  };")
    return lines


def render_struct(block: StructBlock) -> str:
    lines: List[str] = []
    lines.append(f"struct {block.name} {{")
    lines.append("  struct __layout {")

    cursor_expr = "0"
    union_fields = [f for f in block.fields if f.kind == "union"]

    for field in block.fields:
        if field.kind == "union":
            lines.append(f"    static constexpr std::size_t {field.name}_tag_offset = {cursor_expr};")
            lines.append(
                f"    static constexpr std::size_t {field.name}_payload_offset = {field.name}_tag_offset + noserde::wire_sizeof<std::uint32_t>();"
            )
            payload_sizes = ", ".join(
                field_size_expr(alt.type_name, alt.is_record) for alt in field.union_alts
            )
            lines.append(
                f"    static constexpr std::size_t {field.name}_payload_size = noserde::max_size({payload_sizes});"
            )
            cursor_expr = f"{field.name}_payload_offset + {field.name}_payload_size"
        else:
            lines.append(f"    static constexpr std::size_t {field.name}_offset = {cursor_expr};")
            cursor_expr = (
                f"{field.name}_offset + {field_size_expr(field.type_name, field.kind == 'record')}"
            )

    lines.append(f"    static constexpr std::size_t size_bytes = {cursor_expr};")
    lines.append("  };")
    lines.append("")

    for union_field in union_fields:
        lines.append(f"  struct {union_field.union_type_name} {{")
        for alt in union_field.union_alts:
            if alt.inline_type_name:
                lines.append(f"    using {alt.inline_type_name} = {alt.type_name};")
        lines.append("  };")
        lines.append("")

    for union_field in union_fields:
        alt_types = ", ".join(data_type_expr_for_alt(alt) for alt in union_field.union_alts)
        lines.append(f"  using {union_field.name}_data = std::variant<{alt_types}>;")
    if union_fields:
        lines.append("")

    lines.append("  struct Data {")
    for field in block.fields:
        field_data_type = f"{field.name}_data" if field.kind == "union" else data_type_expr_for_field(field)
        lines.append(f"    {field_data_type} {field.name}{{}};")
    lines.append("  };")
    lines.append("")

    for union_field in union_fields:
        lines.extend(render_union_class(union_field, const_ref=False))
        lines.append("")
        lines.extend(render_union_class(union_field, const_ref=True))
        lines.append("")

    # Ref class
    lines.append("  class Ref {")
    lines.append("   private:")
    lines.append("    std::byte* base_;")
    lines.append("")
    lines.append("   public:")

    for field in block.fields:
        if field.kind == "record":
            field_type = f"typename noserde::record_traits<{field.type_name}>::ref"
        elif field.kind == "union":
            field_type = f"{field.name}_union_ref"
        else:
            field_type = f"noserde::scalar_ref<{field.type_name}>"
        lines.append(f"    {field_type} {field.name};")

    init_list = ["base_(base)"]
    for field in block.fields:
        if field.kind == "record":
            init_list.append(
                f"{field.name}(noserde::make_record_ref<{field.type_name}>(base + __layout::{field.name}_offset))"
            )
        elif field.kind == "union":
            init_list.append(
                f"{field.name}(base + __layout::{field.name}_tag_offset, base + __layout::{field.name}_payload_offset)"
            )
        else:
            init_list.append(f"{field.name}(base + __layout::{field.name}_offset)")

    lines.append("")
    lines.append("    explicit Ref(std::byte* base)")
    lines.append("        : " + ",\n          ".join(init_list) + " {}")
    lines.append("  };")
    lines.append("")

    # ConstRef class
    lines.append("  class ConstRef {")
    lines.append("   private:")
    lines.append("    const std::byte* base_;")
    lines.append("")
    lines.append("   public:")

    for field in block.fields:
        if field.kind == "record":
            field_type = f"typename noserde::record_traits<{field.type_name}>::const_ref"
        elif field.kind == "union":
            field_type = f"{field.name}_union_const_ref"
        else:
            field_type = f"noserde::scalar_cref<{field.type_name}>"
        lines.append(f"    {field_type} {field.name};")

    init_list = ["base_(base)"]
    for field in block.fields:
        if field.kind == "record":
            init_list.append(
                f"{field.name}(noserde::make_record_const_ref<{field.type_name}>(base + __layout::{field.name}_offset))"
            )
        elif field.kind == "union":
            init_list.append(
                f"{field.name}(base + __layout::{field.name}_tag_offset, base + __layout::{field.name}_payload_offset)"
            )
        else:
            init_list.append(f"{field.name}(base + __layout::{field.name}_offset)")

    lines.append("")
    lines.append("    explicit ConstRef(const std::byte* base)")
    lines.append("        : " + ",\n          ".join(init_list) + " {}")
    lines.append("  };")
    lines.append("")

    hash_value = schema_hash64(block)
    lines.append("  static constexpr std::size_t noserde_size_bytes = __layout::size_bytes;")
    lines.append(f"  static constexpr std::uint64_t noserde_schema_hash = 0x{hash_value:016x}ULL;")
    lines.append("")
    lines.append("  static void assign_data(Ref dst, const Data& src) {")
    for field in block.fields:
        if field.kind == "record":
            lines.append(f"    {field.type_name}::assign_data(dst.{field.name}, src.{field.name});")
        elif field.kind == "union":
            lines.append("    std::visit(")
            lines.append("        [&](const auto& value) {")
            lines.append("          using Alt = std::decay_t<decltype(value)>;")
            for idx, alt in enumerate(field.union_alts):
                cond = "if" if idx == 0 else "else if"
                data_alt = data_type_expr_for_alt(alt)
                lines.append(f"          {cond} constexpr (std::is_same_v<Alt, {data_alt}>) {{")
                if alt.is_record:
                    lines.append(f"            dst.{field.name}.template emplace<{alt.type_name}>();")
                    lines.append(
                        f"            auto* value_ref = dst.{field.name}.template get_if<{alt.type_name}>();"
                    )
                    lines.append("            if (value_ref == nullptr) {")
                    lines.append("              std::abort();")
                    lines.append("            }")
                    lines.append(f"            {alt.type_name}::assign_data(*value_ref, value);")
                else:
                    lines.append(f"            dst.{field.name}.template emplace<{alt.type_name}>(value);")
                lines.append("          }")
            lines.append("          else {")
            lines.append("            std::abort();")
            lines.append("          }")
            lines.append("        },")
            lines.append(f"        src.{field.name});")
        else:
            lines.append(f"    dst.{field.name} = static_cast<{field.type_name}>(src.{field.name});")
    lines.append("  }")
    lines.append("")
    lines.append("  static Ref make_ref(std::byte* ptr) { return Ref(ptr); }")
    lines.append("  static ConstRef make_const_ref(const std::byte* ptr) { return ConstRef(ptr); }")
    lines.append("};")

    return "\n".join(lines)


def apply_substitutions(source: str, blocks: Sequence[StructBlock]) -> str:
    pieces: List[str] = []
    cursor = 0
    has_noserde_include = bool(
        re.search(r'^\s*#\s*include\s*[<"]noserde\.hpp[>"]', source, re.MULTILINE)
    )
    injected_include = False

    for block in blocks:
        pieces.append(source[cursor : block.start])
        rendered_records: List[str] = []
        for helper in block.helpers:
            rendered_records.append(render_struct(helper))
        rendered_records.append(render_struct(block))
        replacement = "\n\n".join(rendered_records)
        if not has_noserde_include and not injected_include:
            replacement = "#include <noserde.hpp>\n\n" + replacement
            injected_include = True
        pieces.append(replacement)
        cursor = block.end

    pieces.append(source[cursor:])
    return "".join(pieces)


def compute_file_digest(source_bytes: bytes) -> str:
    h = hashlib.sha256()
    h.update(GENERATOR_VERSION.encode("utf-8"))
    h.update(b"\x00")
    h.update(FORMAT_VERSION.encode("utf-8"))
    h.update(b"\x00")
    h.update(source_bytes)
    return h.hexdigest()


def render_file(source_path: pathlib.Path, source_text: str, source_bytes: bytes) -> str:
    blocks = parse_all_structs(source_text)
    for block in blocks:
        block.fields = parse_struct_fields(block.body, block.start)

    classify_fields(blocks)
    transformed = apply_substitutions(source_text, blocks)
    digest = compute_file_digest(source_bytes)
    source_label = str(source_path)
    try:
        source_label = str(source_path.resolve().relative_to(pathlib.Path.cwd().resolve()))
    except ValueError:
        source_label = str(source_path.resolve())

    meta = (
        "// noserde-generated\n"
        f"// source: {source_label}\n"
        f"// generator_version: {GENERATOR_VERSION}\n"
        f"// format_version: {FORMAT_VERSION}\n"
        f"// digest: {digest}\n\n"
    )
    return meta + transformed


def extract_existing_digest(text: str) -> str | None:
    m = DIGEST_PATTERN.search(text)
    if not m:
        return None
    return m.group(1)


def run(args: argparse.Namespace) -> int:
    in_path = pathlib.Path(args.input)
    out_path = pathlib.Path(args.output)

    if not in_path.exists():
        print(f"error: input file does not exist: {in_path}", file=sys.stderr)
        return 1

    source_bytes = in_path.read_bytes()
    source_text = source_bytes.decode("utf-8")

    try:
        rendered = render_file(in_path, source_text, source_bytes)
    except ParseError as e:
        fail(in_path, source_text, e)
        return 1

    if args.check:
        if not out_path.exists():
            print(f"{out_path} is missing (run generator)", file=sys.stderr)
            return 1
        existing = out_path.read_text(encoding="utf-8")
        if existing != rendered:
            print(f"{out_path} is out of date (run generator)", file=sys.stderr)
            return 1
        print(f"up-to-date: {out_path}")
        return 0

    if out_path.exists():
        existing = out_path.read_text(encoding="utf-8")
        old_digest = extract_existing_digest(existing)
        new_digest = extract_existing_digest(rendered)
        if old_digest and new_digest and old_digest == new_digest:
            print(f"unchanged: {out_path}")
            return 0
        if existing == rendered:
            print(f"unchanged: {out_path}")
            return 0

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(rendered, encoding="utf-8")
    print(f"generated: {out_path}")
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate noserde headers from .h.noserde sources")
    parser.add_argument("--in", dest="input", required=True, help="Input .h.noserde file")
    parser.add_argument("--out", dest="output", required=True, help="Output generated header")
    parser.add_argument("--check", action="store_true", help="Check output is up to date")
    return parser


if __name__ == "__main__":
    raise SystemExit(run(build_arg_parser().parse_args()))
