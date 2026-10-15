#!/usr/bin/env python3
"""Unit tests of the pure functions of llasmlib (no D build needed).

The expected values are what llasm.d gives; the differential tests of the
port (llasm_edge.py, llasm_fuzz.py, llasm_compare.py) are the authority.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

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


if __name__ == "__main__":
    unittest.main()
