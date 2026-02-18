#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest

GENERATOR_PATH: pathlib.Path | None = None
REPO_ROOT: pathlib.Path | None = None


class GeneratorBehaviorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if GENERATOR_PATH is None or REPO_ROOT is None:
            raise RuntimeError("generator path and repo root must be initialized in __main__")
        cls.generator = GENERATOR_PATH
        cls.repo_root = REPO_ROOT

    def run_gen(self, in_path: pathlib.Path, out_path: pathlib.Path, check: bool = False) -> subprocess.CompletedProcess[str]:
        cmd = [
            sys.executable,
            str(self.generator),
            "--in",
            str(in_path),
            "--out",
            str(out_path),
        ]
        if check:
            cmd.append("--check")
        return subprocess.run(cmd, cwd=self.repo_root, text=True, capture_output=True)

    def test_targeted_substitution_and_passthrough(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            #include <cstdint>
            // [[noserde]] in comment should remain untouched
            static const char* kToken = "[[noserde]] in string";

            struct Passthrough {
              int k;
            };

            [[noserde]] struct Demo {
              std::uint32_t id;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "demo.h"
            out_path = tmp / "demo.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertEqual(result.returncode, 0, msg=result.stderr)

            generated = out_path.read_text(encoding="utf-8")
            self.assertIn("struct Passthrough", generated)
            self.assertIn("[[noserde]] in comment should remain untouched", generated)
            self.assertIn('"[[noserde]] in string"', generated)
            self.assertIn("struct Demo {", generated)
            self.assertIn("noserde_size_bytes", generated)
            self.assertEqual(generated.count("#include <noserde.hpp>"), 1)

    def test_check_mode_reports_drift(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            [[noserde]] struct A {
              std::uint8_t x;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "a.h"
            out_path = tmp / "a.gen.h"
            in_path.write_text(source, encoding="utf-8")

            first = self.run_gen(in_path, out_path)
            self.assertEqual(first.returncode, 0, msg=first.stderr)

            check_ok = self.run_gen(in_path, out_path, check=True)
            self.assertEqual(check_ok.returncode, 0, msg=check_ok.stderr)
            self.assertIn("up-to-date", check_ok.stdout)

            in_path.write_text(source + "// changed\n", encoding="utf-8")
            check_bad = self.run_gen(in_path, out_path, check=True)
            self.assertNotEqual(check_bad.returncode, 0)
            self.assertIn("out of date", check_bad.stderr)

    def test_pointer_field_rejected_with_location(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            [[noserde]] struct Bad {
              int* ptr;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "bad.h"
            out_path = tmp / "bad.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("pointers/references are not supported", result.stderr)
            self.assertRegex(result.stderr, r"bad\.h:\d+:\d+: error:")

    def test_variant_record_alternative_codegen(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            #include <cstdint>
            [[noserde]] struct Inner {
              std::uint16_t x;
            };

            [[noserde]] struct Outer {
              noserde::variant<Inner, std::uint32_t> v;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "outer.h"
            out_path = tmp / "outer.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertEqual(result.returncode, 0, msg=result.stderr)

            generated = out_path.read_text(encoding="utf-8")
            self.assertIn("class v_variant_ref {", generated)
            self.assertIn("using v_data = std::variant<Inner::Data, std::uint32_t>;", generated)
            self.assertIn("v_tag_offset", generated)
            self.assertIn("static void assign_data(Ref dst, const Data& src)", generated)
            self.assertIn("std::visit(", generated)
            self.assertIn("record alternatives support only default emplace() in v1", generated)
            self.assertNotIn(" get<", generated)

    def test_union_storage_codegen(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            #include <cstdint>
            [[noserde]] struct Inner {
              std::uint16_t x;
            };

            [[noserde]] struct Outer {
              noserde::union_<Inner, std::uint32_t> v;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "outer_union.h"
            out_path = tmp / "outer_union.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertEqual(result.returncode, 0, msg=result.stderr)

            generated = out_path.read_text(encoding="utf-8")
            self.assertIn("class v_union_ref {", generated)
            self.assertIn("auto as() {", generated)
            self.assertIn("using v_data = std::variant<Inner::Data, std::uint32_t>;", generated)
            self.assertNotIn("v_tag_offset", generated)

    def test_named_inline_struct_field_and_variant_codegen(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            [[noserde]] struct Inline {
              struct Meta { std::int16_t x; bool y; } meta;
              noserde::variant<std::uint32_t, double> payload;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "inline.h"
            out_path = tmp / "inline.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertEqual(result.returncode, 0, msg=result.stderr)

            generated = out_path.read_text(encoding="utf-8")
            self.assertIn("struct Inline__meta__Meta {", generated)
            self.assertIn("class payload_variant_ref {", generated)
            self.assertIn("using payload_data = std::variant<std::uint32_t, double>;", generated)
            self.assertIn("type_count<Alternative>() == 1u", generated)
            self.assertIn("meta_offset", generated)
            self.assertIn("payload_tag_offset", generated)

    def test_union_keyword_rejected(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            [[noserde]] struct Inline {
              union Legacy {
                std::int16_t x;
                bool y;
              } payload;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "inline_bad_union.h"
            out_path = tmp / "inline_bad_union.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("C++ union fields are no longer supported", result.stderr)

    def test_inline_variant_alternative_rejected(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            [[noserde]] struct Inline {
              noserde::variant<struct Words { std::uint32_t hi; std::uint32_t lo; }, double> payload;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "inline_bad_variant_alt.h"
            out_path = tmp / "inline_bad_variant_alt.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("inline aggregate alternatives are not supported", result.stderr)

    def test_anonymous_inline_struct_rejected(self) -> None:
        source = textwrap.dedent(
            """
            #pragma once
            [[noserde]] struct Inline {
              struct { std::int16_t x; bool y; } meta;
            };
            """
        ).strip() + "\n"

        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            in_path = tmp / "inline_bad.h"
            out_path = tmp / "inline_bad.gen.h"
            in_path.write_text(source, encoding="utf-8")

            result = self.run_gen(in_path, out_path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("anonymous nested structs are not supported", result.stderr)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: generator_tests.py <generator_path> <repo_root>")
    GENERATOR_PATH = pathlib.Path(sys.argv[1]).resolve()
    REPO_ROOT = pathlib.Path(sys.argv[2]).resolve()
    sys.argv = [sys.argv[0]]
    unittest.main()
