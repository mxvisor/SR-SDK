#!/usr/bin/env python3
"""Unit tests of llasmlib (no D build needed).

The expected values of the pure functions are what llasm.d gives; the
differential tests of the port (llasm_edge.py, llasm_fuzz.py, llasm_compare.py)
are the authority for the language the two share. PortOnly checks what only
llasm.py has (PORT_ONLY of llasm_fuzz.py), which nothing can be compared with.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

LLASM_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, LLASM_DIR)

from llasmlib import expr, strutil, tables


class Numbers(unittest.TestCase):
    def test_is_number(self):
        self.assertTrue(expr.is_number("0x1F"))
        self.assertTrue(expr.is_number("-12"))
        self.assertFalse(expr.is_number("abc"))
        self.assertFalse(expr.is_number("0xZZ"))

    def test_value(self):
        self.assertEqual(expr.get_number_value("0x1F"), 31)
        self.assertEqual(expr.get_number_value("-1"), -1)


class Expressions(unittest.TestCase):
    def test_arithmetic(self):
        self.assertEqual(expr.calculate_expr_value("1+2*3"), (True, "7"))
        self.assertEqual(expr.calculate_expr_value("(1<<4)|3"), (True, "19"))
        self.assertEqual(expr.calculate_expr_value("0x10-1"), (True, "15"))

    def test_errors(self):
        self.assertFalse(expr.calculate_expr_value("7/0")[0])
        self.assertFalse(expr.calculate_expr_value("(1+2")[0])
        self.assertFalse(expr.calculate_expr_value("1+2)")[0])


class Strings(unittest.TestCase):
    def test_strip(self):
        self.assertEqual(strutil.strip("  x \n"), "x")
        self.assertEqual(strutil.lstrip("  x "), "x ")
        self.assertEqual(strutil.rstrip("  x "), "  x")

    def test_split(self):
        self.assertEqual(strutil.str_split_strip(" a , b ,c", ","), [" a", "b", "c"])


class Names(unittest.TestCase):
    def test_llref(self):
        self.assertEqual(tables._llref("a.b"), "@a.b")
        self.assertEqual(tables._llref("1x"), '@"1x"')

    def test_tables(self):
        self.assertEqual(tables.registers_base_list[:3], ["eax", "ecx", "edx"])
        self.assertEqual(len(tables.registers_base_list), tables.NUM_REGS + 22)


class PortOnly(unittest.TestCase):
    """not reg, and store8/store16 to a data label: llasm.d has neither."""
    PROGRAM = """datasegment seg0
dlabel dl_one global
db 4 dup 1
endd
proc p_main public
    not eax
    store8 eax, dl_one, 1
    store16 ecx, dl_one, 2
endp
"""
    # the flags of llasm_fuzz.py's first set, and those of a game's build (scons/game.py)
    FLAGS = ([], ["-O", "-m64", "-inline-idiv", "-pic", "-ptrofs"])

    def llasm(self, flags):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "t.llasm")
            with open(src, "w") as f:
                f.write(self.PROGRAM)
            r = subprocess.run([sys.executable, os.path.join(LLASM_DIR, "llasm.py"), src] + flags,
                               capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout

    def test_not(self):
        for flags in self.FLAGS:
            ir = self.llasm(flags)
            self.assertRegex(ir, r";    not eax\n(?:.*\n)*?%(\d+) = xor i32 %\d+, -1\n(?:.*\n)*?store i32 %\1, i32\* %\d+, align 4\n")

    def test_store_to_label(self):
        for flags in self.FLAGS:
            ir = self.llasm(flags)
            self.assertRegex(ir, r"%(\d+) = trunc i32 %\d+ to i8\nstore i8 %\1, i8\* @dl_one, align 1\n")
            self.assertRegex(ir, r"%(\d+) = trunc i32 %\d+ to i16\n"
                                 r"store i16 %\1, i16\* bitcast \(i8\* @dl_one to i16\*\), align 2\n")


if __name__ == "__main__":
    unittest.main()
