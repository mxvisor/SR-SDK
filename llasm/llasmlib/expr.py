#
#  Copyright (C) 2019-2026 Roman Pauer (llasm.d)
#  Copyright (C) 2026 mxvisor
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of
#  this software and associated documentation files (the "Software"), to deal in
#  the Software without restriction, including without limitation the rights to
#  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
#  of the Software, and to permit persons to whom the Software is furnished to do
#  so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE.

"""Numbers and constant expressions."""

from .state import G, stderr_write
from .strutil import lstrip, rstrip, strip
from .tables import _DEC_RE, _HEXDIGITS


def _to_long(s):
    # std.conv.to!long: optional sign, decimal digits only, 64-bit range
    if not _DEC_RE.match(s):
        raise ValueError(s)
    v = int(s)
    if v < -(1 << 63) or v >= (1 << 63):
        raise ValueError(s)
    return v


def _wrap64(v):
    v &= (1 << 64) - 1
    if v >= (1 << 63):
        v -= (1 << 64)
    return v


def is_number(str_num):
    if len(str_num) > 2 and str_num[0] == '0' and str_num[1] == 'x':
        for c in str_num[2:].lower():
            if _HEXDIGITS.find(c) == -1:
                return False
        return True
    else:
        try:
            _to_long(str_num)
            return True
        except ValueError:
            return False


def get_number_value(str_num):
    if len(str_num) > 2 and str_num[0] == '0' and str_num[1] == 'x':
        result = 0
        for c in str_num[2:].lower():
            result = _wrap64(result * 16 + _HEXDIGITS.find(c))
        return result
    else:
        return _to_long(str_num)


def _find_delims(expr, chars):
    nextpos = -1
    next_delim = ""
    for c in chars:
        pos = expr.find(c)
        if pos != -1:
            if nextpos == -1 or nextpos > pos:
                nextpos = pos
                next_delim = c
    return nextpos, next_delim


def _find_shift(expr):
    shlpos = expr.find("<<")
    shrpos = expr.find(">>")
    shift_delim = ""
    if shlpos != -1:
        if shrpos != -1:
            if shrpos < shlpos:
                shiftpos = shrpos
                shift_delim = ">>"
            else:
                shiftpos = shlpos
                shift_delim = "<<"
        else:
            shiftpos = shlpos
            shift_delim = "<<"
    else:
        shiftpos = shrpos
        if shrpos != -1:
            shift_delim = ">>"
    return shiftpos, shift_delim


def _u32(v):
    return v & 0xFFFFFFFF


def calculate_expr_value2(expr):
    """Returns (ok, expr); expr is only changed on success (D: ref string)."""
    expr = strip(expr)

    if expr == "":
        return False, expr

    tokens = []
    last_delim = ""

    while expr != "":
        nextpos, next_delim = _find_delims(expr, "+-*&|^~")
        shiftpos, shift_delim = _find_shift(expr)

        if last_delim != "":
            tokens.append([last_delim, ""])

        if nextpos == -1 and shiftpos == -1:
            expr_part = expr
            expr = ""
            last_delim = ""
        elif shiftpos == -1 or (nextpos != -1 and nextpos < shiftpos):
            expr_part = rstrip(expr[0:nextpos])
            expr = lstrip(expr[nextpos + 1:])
            last_delim = next_delim
        else:
            expr_part = rstrip(expr[0:shiftpos])
            expr = lstrip(expr[shiftpos + 2:])
            last_delim = shift_delim

        if expr_part == "":
            pass
        elif is_number(expr_part):
            if expr_part != "":
                tokens.append(["", expr_part])
        else:
            return False, expr

    if last_delim != "":
        tokens.append([last_delim, ""])

    # unary ops
    i = len(tokens) - 1
    while i >= 0:
        op = tokens[i][0]

        if op == "" or (op != "~" and op != "-"):
            i -= 1
            continue

        if i == len(tokens) - 1:
            return False, expr
        if tokens[i + 1][0] != "":
            return False, expr

        if op == "-" and i > 0 and tokens[i + 1][0] == "":
            i -= 1
            continue

        value = get_number_value(tokens[i + 1][1])

        if op == "-":
            value = _wrap64(-value)
        else:
            value = ~value

        tokens = tokens[0:i] + [["", str(_u32(value))]] + tokens[i + 2:]
        i -= 1

    # binary ops
    precedence = [["*"], ["&", "|", "^"], ["<<", ">>"], ["+", "-"]]
    for op_prec in precedence:
        i = len(tokens) - 1
        while i >= 0:
            op = tokens[i][0]

            if op == "":
                i -= 1
                continue

            if op not in op_prec:
                i -= 1
                continue

            if i == len(tokens) - 1:
                return False, expr
            if tokens[i + 1][0] != "":
                return False, expr

            if i == 0:
                return False, expr
            if tokens[i - 1][0] != "":
                return False, expr

            value0 = get_number_value(tokens[i - 1][1])
            value1 = get_number_value(tokens[i + 1][1])

            if op == "+":
                value0 = _wrap64(value0 + value1)
            elif op == "-":
                value0 = _wrap64(value0 - value1)
            elif op == "*":
                value0 = _wrap64(value0 * value1)
            elif op == "&":
                value0 &= value1
            elif op == "|":
                value0 |= value1
            elif op == "^":
                value0 ^= value1
            elif op == "<<":
                value0 = _wrap64(value0 << (value1 & 63))
            elif op == ">>":
                value0 = value0 >> (value1 & 63)
            else:
                return False, expr

            tokens = tokens[0:i - 1] + [["", str(_u32(value0))]] + tokens[i + 2:]
            i -= 1
            # the D loop does i-- here and then i-- again in the for statement
            i -= 1

    if len(tokens) == 1 and tokens[0][0] == "":
        value = get_number_value(tokens[0][1])
        return True, str(_u32(value))

    return False, expr


def calculate_expr_value(param):
    """Returns (ok, param); D takes param by reference."""
    while True:
        position = param.find(')')
        if position >= 0:
            part3 = param[position + 1:]
            part1 = param[0:position]

            position = part1.rfind('(')
            if position >= 0:
                part2 = part1[position + 1:]
                part1 = part1[0:position]

                ok, part2 = calculate_expr_value2(part2)
                if not ok:
                    return False, param

                param = part1 + " " + part2 + " " + part3
            else:
                return False, param
        else:
            if param.find('(') >= 0:
                return False, param
            break

    return calculate_expr_value2(param)


def replace_defines_in_expr2(expr, used_defines):
    """Returns (ok, res)."""
    last_delim = ""
    res = ""

    while expr != "":
        nextpos, next_delim = _find_delims(expr, "+-*&|^~()")
        shiftpos, shift_delim = _find_shift(expr)

        res = res + last_delim

        if nextpos == -1 and shiftpos == -1:
            expr_part = expr
            expr = ""
            last_delim = ""
        elif shiftpos == -1 or (nextpos != -1 and nextpos < shiftpos):
            expr_part = rstrip(expr[0:nextpos])
            expr = lstrip(expr[nextpos + 1:])
            last_delim = next_delim
        else:
            expr_part = rstrip(expr[0:shiftpos])
            expr = lstrip(expr[shiftpos + 2:])
            last_delim = shift_delim

        if expr_part == "":
            pass
        elif is_number(expr_part):
            res = res + expr_part
        elif expr_part in G.define_list:
            for u in used_defines:
                if expr_part == u:
                    stderr_write("Recursive define: " + expr_part)
                    return False, res

            ok, expr_part2 = replace_defines_in_expr2(G.define_list[expr_part], used_defines + [expr_part])
            if not ok:
                return False, res

            res = res + expr_part2
        else:
            return False, res

    res = res + last_delim

    return True, res


def replace_defines_in_expr(param):
    """Returns (ok, param); D takes param by reference."""
    ok, expr2 = replace_defines_in_expr2(param, [])
    if ok:
        param = expr2
    return ok, param


def _s32(v):
    v &= 0xFFFFFFFF
    if v >= 0x80000000:
        v -= 0x100000000
    return v


def _to_int(s):
    v = _to_long(s)
    if v < -0x80000000 or v > 0x7FFFFFFF:
        raise OverflowError("Overflow in integer conversion")
    return v
