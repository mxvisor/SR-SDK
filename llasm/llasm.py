#!/usr/bin/env python3
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
#

"""Python port of llasm.d (SR's llasm to LLVM IR translator).

The aim is byte-identical output, so the structure, the order of operations
and the counters (temporary registers, labels) deliberately follow llasm.d.
"""

import os
import re
import sys

WS = " \t\n\v\f\r"

_LL_NAME_OK = re.compile(r'[a-zA-Z$._][a-zA-Z$._0-9]*\Z')

def _llref(name):
    """LLVM IR global value reference; quote names that are not bare identifiers
    (stdcall imports like _grBufferClear@12 contain @)."""
    if name and _LL_NAME_OK.match(name):
        return "@" + name
    return '@"' + name.replace('\\', '\\\\').replace('"', '\\"') + '"'


NUM_REGS = 12
NUM_REGS_VALID = 9
NUM_FLOAT_REGS = 8
NUM_REGS64 = 1
REGS64_START_NUM = 13

EMPTY, READ, WRITE = 0, 1, 2

registers_base_list = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "eflags", "st_top", "st_sw_cond", "st_cw", "tmpadr", "tmpcnd", "tmp0", "tmp1", "tmp2", "tmp3", "tmp4", "tmp5", "tmp6", "tmp7", "tmp8", "tmp9", "tmp10", "tmp11", "tmp12", "tmp13", "tmp14", "tmp15", "tmp16", "tmp17", "tmp18", "tmp19"]
registers64_base_list = ["ptrofs"]
keywords_base_list = ["proc", "extern", "define", "macro", "func", "funcp", "funcv", "include", "endp", "endm", "datasegment", "dlabel", "dalign", "db", "dinclude", "daddr", "dskip", "endd"]
instructions_base_list = [
    "mov reg, reg/const/procaddr/externaddr",
    "add reg, reg, reg/const",
    "sub reg, reg/const, reg/const",
    "shl reg, reg, reg/const",
    "lshr reg, reg, reg/const",
    "ashr reg, reg, reg/const",
    "and reg, reg, reg/const",
    "or reg, reg, reg/const",
    "xor reg, reg, reg/const",
    "load reg, reg/externaddr, const",
    "load8s reg, reg/externaddr, const",
    "load8z reg, reg/externaddr, const",
    "load16s reg, reg/externaddr, const",
    "load16z reg, reg/externaddr, const",
    "store reg/const/procaddr/externaddr, reg/externaddr, const",
    "store8 reg/const, reg/externaddr/addr, const",
    "store16 reg/const, reg/externaddr/addr, const",
    "ext8s reg, reg",
    "ext16s reg, reg",
    "ins16 reg, reg, reg",
    "ins8ll reg, reg, reg",
    "ins8lh reg, reg, reg",
    "ins8hl reg, reg, reg",
    "ins8hh reg, reg, reg",
    "tcall reg/procaddr/externaddr",
    "ctcallz reg, procaddr",
    "ctcallnz reg, procaddr",
    "cmovz reg, reg, reg/const, reg/const",
    "cmoveq reg, reg/const, reg, reg/const, reg/const",
    "cmovult reg, reg/const, reg, reg/const, reg/const",
    "cmovugt reg, reg/const, reg, reg/const, reg/const",
    "cmovslt reg, reg/const, reg, reg/const, reg/const",
    "cmovsgt reg, reg/const, reg, reg/const, reg/const",
    "call funcaddr [$/reg/const]",
    "mul reg, reg, reg/const",
    "imul reg, reg, reg, reg/const",
    "umul reg, reg, reg, reg/const",
    "ifz reg",
    "ifnz reg",
    "endif",
    "ctlz reg, reg",
    "bswap reg, reg",
]
instructions_idiv_list = [
    "DIV_64_ZE reg/const",
    "IDIV_64_SE reg/const",
]
instructions_float_list = [
    "FLDCW_UINT16 reg/const",
    "FNSTCW_VOID",
    "FNSTSW_VOID",
    "FCHS_VOID",
    "FXCH_ST const",
    "FILD_INT32 reg/const",
    "FLD_FLOAT reg/const",
    "FLD_ST const",
    "FLD1_VOID",
    "FLDLG2_VOID",
    "FLDLN2_VOID",
    "FLDZ_VOID",
    "FST_FLOAT",
    "FST_ST const",
    "FSTP_FLOAT",
    "FSTP_ST const",
    "FIADD_INT32 reg/const",
    "FADD_FLOAT reg/const",
    "FADD_ST const",
    "FADD_TO_ST const",
    "FADDP_ST const",
    "FISUB_INT32 reg/const",
    "FISUBR_INT32 reg/const",
    "FSUB_FLOAT reg/const",
    "FSUB_ST const",
    "FSUB_TO_ST const",
    "FSUBP_ST const",
    "FSUBR_FLOAT reg/const",
    "FSUBR_ST const",
    "FSUBRP_ST const",
    "FIMUL_INT32 reg/const",
    "FMUL_FLOAT reg/const",
    "FMUL_ST const",
    "FMUL_TO_ST const",
    "FMULP_ST const",
    "FDIV_FLOAT reg/const",
    "FDIV_ST const",
    "FDIV_TO_ST const",
    "FDIVP_ST const",
    "FDIVR_FLOAT reg/const",
    "FDIVR_ST const",
    "FDIVRP_ST const",
    "FIDIV_INT32 reg/const",
    "FIDIVR_INT32 reg/const",
    "FCOM_FLOAT reg/const",
    "FCOM_ST const",
    "FCOMP_FLOAT reg/const",
    "FCOMP_ST const",
    "FCOMPP_VOID",
    "FUCOM_ST const",
    "FUCOMP_ST const",
    "FUCOMPP_VOID",
    "FSQRT_VOID",
    "FABS_VOID",
]
instructions_float2_list = [
    "FMOD_VOID",
    "FMODR_VOID",
    "FSIN_VOID",
    "FCOS_VOID",
    "FTAN_VOID",
    "FLOG_VOID",
    "FLOG10_VOID",
    "FROUND_VOID",
    "FPOW_VOID",
    "FPOWR_VOID",
    "FYL2X_VOID",
]


def strip(s):
    return s.strip(WS)


def lstrip(s):
    return s.lstrip(WS)


def rstrip(s):
    return s.rstrip(WS)


class Param:
    """param_struct"""
    __slots__ = ("type", "value", "ivalue")

    def __init__(self, type="", value="", ivalue=0):
        self.type = type
        self.value = value
        self.ivalue = ivalue


class Line:
    """input_line_struct"""
    __slots__ = ("orig", "word", "params", "param1")

    def __init__(self):
        self.orig = ""
        self.word = ""
        self.params = ""
        self.param1 = ""


class FileInput:
    """file_input_struct"""

    def __init__(self):
        self.ismacro = False
        self.name = ""
        self.fullpath = ""
        self.linenum = 0
        self.lines = None      # file contents, split into lines
        self.params = []


class MacroDef:
    def __init__(self):
        self.name = ""
        self.params = []
        self.index = []
        self.lines = []


class FuncParam:
    def __init__(self, name="", type=0):
        self.name = name
        self.type = type       # 0 Integer, 1 Pointer, 2 Cpu


class FuncDef:
    def __init__(self):
        self.real_name = ""
        self.has_return_value = False
        self.return_value_is_pointer = False
        self.params = []


class ProcDef:
    def __init__(self):
        self.name = ""
        self.alias_name = ""
        self.isexternal = False
        self.ispublic = False
        self.isglobal = False
        self.global_name = ""
        self.lines = []


class InstrDef:
    def __init__(self):
        self.name = ""
        self.params = []
        self.params2 = []
        self.variable_params = ""
        self.variable_params2 = {}
        self.inlined_instr = False


class ProcInstr:
    """proc_instr_struct"""

    def __init__(self):
        self.is_valid = False
        self.is_inside_if = False
        self.func_has_return_value = False
        self.if_contains_call = False
        self.if_changes_st_top = False
        self.float_stack_top = 0
        self.write_reg = ["", "", ""]
        self.write_float_reg = [0, 0]
        self.last_reg_write = [False, False, False]
        self.last_float_reg_write = [False, False]
        self.params = []


class DLabel:
    def __init__(self):
        self.name = ""
        self.dataseg_name = ""
        self.offset = 0
        self.isglobal = False


class DData:
    def __init__(self):
        self.isaddr = False
        self.data = bytearray()
        self.datalen = 0
        self.addr = ""
        self.addr_offset = 0


class DataSeg:
    def __init__(self):
        self.name = ""
        self.isconstant = False
        self.isuninitialized = False
        self.addr_in_ctor = False
        self.labels = []
        self.data = []


class State:
    pass


G = State()


def reset_state():
    G.input_filename = ""
    G.output_filename = ""
    G.input_directory = ""
    G.return_procedure = ""
    G.dispatcher_procedure = ""
    G.include_directories = []
    G.output_preprocessed_file = False
    G.input_reading_proc = False
    G.input_reading_dataseg = False
    G.position_independent_code = False
    G.old_bitcode = False
    G.no_tail_calls = False
    G.pointer_size_64 = False
    G.pointers_with_offset = False
    G.inline_idiv_instr = False
    G.inline_float_instr = False
    G.inline_float2_instr = False
    G.global_optimization_level = 0
    G.procedure_optimization_level = 0
    G.function_names = {}

    G.file_input_level = -1
    G.file_input_stack = []
    G.end_of_input = False
    G.current_line = Line()

    G.current_proc = ""
    G.current_dataseg = ""
    G.current_proc_lines = 0
    G.current_dataseg_offset = 0
    G.current_dataseg_numlabels = 0
    G.current_dataseg_numdata = 0
    G.current_dataseg_isuninitialized = False
    G.proc_instructions = []
    G.dataseg_labels = []
    G.dataseg_data = []
    G.temporary_register_index = 0
    G.label_index = 0

    G.macro_list = {}
    G.define_list = {}
    G.extern_list = {}
    G.dataseg_list = {}
    G.dlabel_list = {}
    G.func_list = {}
    G.proc_list = {}
    G.local_proc_names = []

    G.used_ctlz_intrinsics = False
    G.used_bswap_intrinsics = False
    G.used_sqrt_intrinsics = False
    G.used_sin_intrinsics = False
    G.used_cos_intrinsics = False
    G.used_tan_intrinsics = False
    G.used_log_intrinsics = False
    G.used_log10_intrinsics = False
    G.used_fabs_intrinsics = False
    G.used_round_intrinsics = False
    G.used_pow_intrinsics = False
    G.used_log2_intrinsics = False
    G.create_ctor_function = False

    G.output_lines = []

    G.keywords_list = {}
    G.registers_list = {}
    G.temp_regs_list = {}
    G.register_numbers_str = {}
    G.register64_numbers_str = {}
    G.instructions_list = {}
    G.register_state = [EMPTY] * NUM_REGS
    G.float_reg_state = [EMPTY] * NUM_FLOAT_REGS
    G.register64_state = [EMPTY] * NUM_REGS64
    G.current_temporary_register = {}
    G.current_temporary_float_register = {}
    G.current_temporary_register64 = {}

    G.stderr = sys.stderr


reset_state()


def stderr_write(msg):
    G.stderr.write(msg + "\n")


def str_split_strip(s, delim):
    res = []
    while s != "":
        nextpos = s.find(delim)
        if nextpos == -1:
            res.append(s)
            s = ""
        else:
            res.append(rstrip(s[0:nextpos]))
            s = lstrip(s[nextpos + 1:])
    return res


def add_instruction_to_list(instruction, inlined_instr):
    position = instruction.find(' ')

    if position >= 0:
        instruction_name = instruction[0:position]
        instruction = strip(instruction[position + 1:])

        position = instruction.find('[')
        if position >= 0:
            variable_param = strip(instruction[position + 1:])
            instruction = strip(instruction[0:position])
        else:
            variable_param = ""

        params = str_split_strip(instruction, ',')

        if variable_param != "":
            variable_param = strip(variable_param[0:len(variable_param) - 1])
    else:
        instruction_name = instruction
        params = []
        variable_param = ""

    new = InstrDef()
    new.name = instruction_name
    new.params = params
    new.variable_params = variable_param
    new.inlined_instr = inlined_instr

    new.params2 = [dict() for _ in new.params]
    for i in range(len(new.params)):
        for param in str_split_strip(new.params[i], '/'):
            new.params2[i][param] = True

    if variable_param != "":
        for param in str_split_strip(variable_param, '/'):
            new.variable_params2[param] = True

    G.instructions_list[instruction_name] = new


def initialize():
    for register in registers_base_list:
        G.registers_list[register] = True
    for i in range(NUM_REGS_VALID, NUM_REGS):
        G.registers_list[registers_base_list[i]] = False

    for i in range(NUM_REGS):
        G.register_numbers_str[registers_base_list[i]] = str(i)

    for i in range(NUM_REGS64):
        G.register64_numbers_str[registers64_base_list[i]] = str(i + REGS64_START_NUM)

    for i in range(NUM_REGS, len(registers_base_list)):
        G.temp_regs_list[registers_base_list[i]] = True

    for keyword in keywords_base_list:
        G.keywords_list[keyword] = True

    for instruction in instructions_base_list:
        add_instruction_to_list(instruction, False)
    if G.inline_idiv_instr:
        for instruction in instructions_idiv_list:
            add_instruction_to_list(instruction, True)
    if G.inline_float_instr:
        for instruction in instructions_float_list:
            add_instruction_to_list(instruction, True)
    if G.inline_float2_instr:
        for instruction in instructions_float2_list:
            add_instruction_to_list(instruction, True)

    G.used_ctlz_intrinsics = False
    G.used_bswap_intrinsics = False
    G.used_sqrt_intrinsics = False
    G.used_sin_intrinsics = False
    G.used_cos_intrinsics = False
    G.used_tan_intrinsics = False
    G.used_log_intrinsics = False
    G.used_log10_intrinsics = False
    G.used_fabs_intrinsics = False
    G.used_round_intrinsics = False
    G.used_pow_intrinsics = False
    G.used_log2_intrinsics = False
    G.create_ctor_function = False


def _read_file_lines(path):
    # llasm.d reads bytes and keeps them as they are: latin-1 round-trips them
    with open(path, "rb") as f:
        data = f.read().decode("latin-1")
    lines = []
    pos = 0
    n = len(data)
    while pos < n:
        nl = data.find("\n", pos)
        if nl == -1:
            lines.append(data[pos:])
            break
        lines.append(data[pos:nl + 1])
        pos = nl + 1
    return lines


def read_next_line():
    while G.file_input_level >= 0:
        cur = G.file_input_stack[G.file_input_level]

        if cur.ismacro:
            curmacro = G.macro_list[cur.name]

            if cur.linenum >= len(curmacro.lines):
                cur.params = []
                G.file_input_level -= 1
                continue

            buf = curmacro.lines[cur.linenum]

            for index in curmacro.index:
                buf = buf.replace("\\" + curmacro.params[index], cur.params[index])
        else:
            if cur.linenum >= len(cur.lines):
                cur.lines = None
                G.file_input_level -= 1
                continue

            buf = cur.lines[cur.linenum]

            # remove return from end of the line
            while len(buf) > 0 and (buf[-1] == '\r' or buf[-1] == '\n'):
                buf = buf[:-1]

        cur.linenum += 1
        G.current_line = Line()
        G.current_line.orig = buf

        position = buf.find(';')
        if position >= 0:
            buf = buf[0:position]

        buf = strip(buf).replace("\t", " ")

        position = buf.find(' ')
        if position >= 0:
            G.current_line.word = buf[0:position]
            buf = lstrip(buf[position + 1:])

            G.current_line.params = buf

            position = buf.find(' ')
            if position >= 0:
                G.current_line.param1 = buf[0:position]
            else:
                G.current_line.param1 = buf
        else:
            G.current_line.word = buf
            G.current_line.params = ""
            G.current_line.param1 = ""

        return

    G.end_of_input = True


def write_error(error_string):
    stderr_write(error_string)
    for i in range(G.file_input_level, -1, -1):
        e = G.file_input_stack[i]
        stderr_write(("Macro" if e.ismacro else "File") + ": " + e.name + " - Line: " + str(e.linenum))


def write_error2(error_string):
    stderr_write(error_string)
    stderr_write("Procedure: " + G.current_proc)


def add_output_line(line):
    G.output_lines.append(line)


def _is_absolute(p):
    return os.path.isabs(p)


def _build_path(*parts):
    # std.path.buildPath: an absolute later part discards the earlier ones
    return os.path.join(*parts)


def add_include_to_stack(include_name):
    fullpath = None

    if not _is_absolute(include_name):
        path = _build_path(G.input_directory, include_name)
        if os.path.exists(path):
            fullpath = os.path.abspath(path)
        else:
            for d in G.include_directories:
                if _is_absolute(d):
                    path = _build_path(d, include_name)
                else:
                    path = _build_path(G.input_directory, d, include_name)

                if os.path.exists(path):
                    fullpath = os.path.abspath(path)
                    break

    if fullpath is None:
        if os.path.exists(include_name):
            fullpath = os.path.abspath(include_name)
        else:
            write_error("Include not found: " + include_name)
            return False

    for i in range(G.file_input_level, -1, -1):
        e = G.file_input_stack[i]
        if not e.ismacro and e.fullpath == fullpath:
            write_error("Recursive include: " + include_name)
            return False

    G.file_input_level += 1
    if G.file_input_level >= len(G.file_input_stack):
        G.file_input_stack.append(FileInput())
    e = G.file_input_stack[G.file_input_level]
    e.ismacro = False
    e.name = include_name
    e.fullpath = fullpath
    e.linenum = 0
    e.lines = _read_file_lines(fullpath)
    e.params = []

    return True


def add_macro_to_stack(macro_name, params):
    for i in range(G.file_input_level, -1, -1):
        e = G.file_input_stack[i]
        if e.ismacro and e.name == macro_name:
            write_error("Recursive macro call: " + macro_name)
            return False

    if len(params) != len(G.macro_list[macro_name].params):
        write_error("Wrong number of macro parameters: " + macro_name)
        return False

    G.file_input_level += 1
    if G.file_input_level >= len(G.file_input_stack):
        G.file_input_stack.append(FileInput())
    e = G.file_input_stack[G.file_input_level]
    e.ismacro = True
    e.name = macro_name
    e.linenum = 0
    e.params = params

    return True


def get_current_params():
    return str_split_strip(G.current_line.params, ',')


def get_current_params2():
    position = G.current_line.params.find(' ')

    if position >= 0:
        return str_split_strip(lstrip(G.current_line.params[position + 1:]), ',')
    else:
        return str_split_strip("", ',')


def get_functions_params(params):
    func_params = [FuncParam() for _ in params]

    for i in range(len(params)):
        if params[i] == "$":
            func_params[i].type = 2
            func_params[i].name = "cpu"
        elif params[i][0] == '*':
            func_params[i].type = 1
            func_params[i].name = lstrip(params[i][1:])
        else:
            func_params[i].type = 0
            func_params[i].name = params[i]

    return func_params


_INTRINSIC_FLAGS = {
    "ctlz": "used_ctlz_intrinsics",
    "bswap": "used_bswap_intrinsics",
    "FSQRT_VOID": "used_sqrt_intrinsics",
    "FSIN_VOID": "used_sin_intrinsics",
    "FCOS_VOID": "used_cos_intrinsics",
    "FTAN_VOID": "used_tan_intrinsics",
    "FLOG_VOID": "used_log_intrinsics",
    "FLOG10_VOID": "used_log10_intrinsics",
    "FABS_VOID": "used_fabs_intrinsics",
    "FROUND_VOID": "used_round_intrinsics",
    "FPOW_VOID": "used_pow_intrinsics",
    "FPOWR_VOID": "used_pow_intrinsics",
    "FYL2X_VOID": "used_log2_intrinsics",
}


def check_instruction_pass1(instr_line):
    instruction = G.instructions_list[instr_line.word]

    if len(instruction.variable_params2) == 0:
        params = str_split_strip(instr_line.params, ',')

        if len(params) != len(instruction.params):
            write_error("Wrong number of instruction parameters: " + instr_line.word)
            return False

        for i in range(len(params)):
            if instruction.params[i] == "reg":
                if (params[i] not in G.registers_list) or (not G.registers_list[params[i]]):
                    write_error("Wrong instruction parameter: " + instr_line.word + ": " + str(i))
                    return False

        flag = _INTRINSIC_FLAGS.get(instr_line.word)
        if flag is not None:
            setattr(G, flag, True)
    else:
        if instr_line.word == "call" and len(instruction.params) == 1:
            if instr_line.params == "":
                write_error("Missing call parameters")
                return False
        else:
            write_error("Instruction error")
            return False

    return True


_HEXDIGITS = "0123456789abcdef"
_DEC_RE = re.compile(r"[+-]?[0-9]+\Z")


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


def is_temporary_register(reg):
    return reg in G.temp_regs_list


def get_new_temporary_register():
    G.temporary_register_index += 1
    return "%" + str(G.temporary_register_index)


def get_new_label():
    G.label_index += 1
    return "l_" + str(G.label_index)


def get_load_type(type):
    if G.old_bitcode:
        return type + "*"
    else:
        return type + ", " + type + "*"


def get_reg64_value(name):
    regnumstr = G.register64_numbers_str[name]

    if G.procedure_optimization_level >= 1:
        regnum = int(regnumstr) - REGS64_START_NUM

        if G.register64_state[regnum] == EMPTY:
            G.register64_state[regnum] = READ

            addr = get_new_temporary_register()
            reg = get_new_temporary_register()
            add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + regnumstr)
            add_output_line(reg + " = load " + get_load_type("i64") + " " + addr + ", align 8")

            if G.procedure_optimization_level >= 2:
                G.current_temporary_register64[name] = reg
            else:
                add_output_line("store i64 " + reg + ", i64* %" + name + ", align 8")
        else:
            if G.procedure_optimization_level >= 2:
                reg = G.current_temporary_register64[name]
            else:
                reg = get_new_temporary_register()
                add_output_line(reg + " = load " + get_load_type("i64") + " %" + name + ", align 8")
    else:
        addr = get_new_temporary_register()
        reg = get_new_temporary_register()
        add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + regnumstr)
        add_output_line(reg + " = load " + get_load_type("i64") + " " + addr + ", align 8")

    return reg


def get_int32_to_ptr(value, ptrtype="i8", nullable_pointer=False):
    if G.pointers_with_offset:
        ptrofs = get_reg64_value("ptrofs")
        addr1 = get_new_temporary_register()
        addr2 = get_new_temporary_register()
        addr3 = get_new_temporary_register()

        add_output_line(addr1 + " = zext i32 " + value + " to i64")
        add_output_line(addr2 + " = add i64 " + addr1 + ", " + ptrofs)
        add_output_line(addr3 + " = inttoptr i64 " + addr2 + " to " + ptrtype + "*")

        if nullable_pointer:
            cond = get_new_temporary_register()
            addr4 = get_new_temporary_register()

            add_output_line(cond + " = icmp eq i64 " + addr1 + ", 0")
            add_output_line(addr4 + " = select i1 " + cond + ", " + ptrtype + "* null, " + ptrtype + "* " + addr3)

            return addr4
        else:
            return addr3
    else:
        addr = get_new_temporary_register()

        add_output_line(addr + " = inttoptr i32 " + value + " to " + ptrtype + "*")

        return addr


def get_addr_to_int32(value, ptrtype="i8", nullable_pointer=False):
    if G.pointers_with_offset:
        ptrofs = get_reg64_value("ptrofs")
        addr1 = get_new_temporary_register()
        addr2 = get_new_temporary_register()
        addr3 = get_new_temporary_register()

        add_output_line(addr1 + " = ptrtoint " + ptrtype + "* " + value + " to i64")
        add_output_line(addr2 + " = sub i64 " + addr1 + ", " + ptrofs)
        add_output_line(addr3 + " = trunc i64 " + addr2 + " to i32")

        if nullable_pointer:
            cond = get_new_temporary_register()
            addr4 = get_new_temporary_register()

            add_output_line(cond + " = icmp eq i64 " + addr1 + ", 0")
            add_output_line(addr4 + " = select i1 " + cond + ", i32 0, i32 " + addr3)

            return addr4
        else:
            return addr3
    else:
        return "ptrtoint (" + ptrtype + "* " + value + " to i32)"


def _extern_is_direct(param):
    return (not G.position_independent_code) or (param.value in G.extern_list) or G.dlabel_list[param.value].isglobal


def _dseg_gep(addrlabel, ofs):
    return "getelementptr (" + get_load_type("i8") + " bitcast (%_" + addrlabel.dataseg_name + "* " + _llref(addrlabel.dataseg_name) + " to i8*), i32 " + str(ofs) + ")"


def get_parameter_read_value(param, as_pointer=False, nullable_pointer=False):
    t = param.type
    if t == "reg":
        if is_temporary_register(param.value):
            if G.procedure_optimization_level >= 2:
                if param.value in G.current_temporary_register:
                    reg = G.current_temporary_register[param.value]
                else:
                    write_error2("Using uninitialized register: " + param.value)
                    return "error"
            else:
                reg = get_new_temporary_register()
                add_output_line(reg + " = load " + get_load_type("i32") + " %" + param.value + ", align 4")
        else:
            regnumstr = G.register_numbers_str[param.value]

            if G.procedure_optimization_level >= 1:
                regnum = int(regnumstr)

                if G.register_state[regnum] == EMPTY:
                    G.register_state[regnum] = READ

                    addr = get_new_temporary_register()
                    reg = get_new_temporary_register()
                    add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + regnumstr)
                    add_output_line(reg + " = load " + get_load_type("i32") + " " + addr + ", align 4")

                    if G.procedure_optimization_level >= 2:
                        G.current_temporary_register[param.value] = reg
                    else:
                        add_output_line("store i32 " + reg + ", i32* %" + param.value + ", align 4")
                else:
                    if G.procedure_optimization_level >= 2:
                        reg = G.current_temporary_register[param.value]
                    else:
                        reg = get_new_temporary_register()
                        add_output_line(reg + " = load " + get_load_type("i32") + " %" + param.value + ", align 4")
            else:
                addr = get_new_temporary_register()
                reg = get_new_temporary_register()
                add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + regnumstr)
                add_output_line(reg + " = load " + get_load_type("i32") + " " + addr + ", align 4")

        if as_pointer:
            return get_int32_to_ptr(reg, "i8", nullable_pointer)
        else:
            return reg
    elif t == "externaddr":
        if _extern_is_direct(param):
            if param.ivalue == 0:
                if as_pointer:
                    return _llref(param.value)
                else:
                    return get_addr_to_int32(_llref(param.value))
            else:
                if as_pointer:
                    return "getelementptr (" + get_load_type("i8") + " " + _llref(param.value) + ", i32 " + str(param.ivalue) + ")"
                else:
                    return get_addr_to_int32("getelementptr (" + get_load_type("i8") + " " + _llref(param.value) + ", i32 " + str(param.ivalue) + ")")
        else:
            addrlabel = G.dlabel_list[param.value]

            if as_pointer:
                return _dseg_gep(addrlabel, _s32(addrlabel.offset + param.ivalue))
            else:
                return get_addr_to_int32(_dseg_gep(addrlabel, _s32(addrlabel.offset + param.ivalue)))
    elif t == "procaddr":
        proctype = ("i8*" if G.no_tail_calls else "void")
        if as_pointer:
            return "bitcast (" + proctype + "(%_cpu*)* " + _llref(param.value) + " to i8*)"
        else:
            return get_addr_to_int32(_llref(param.value), proctype + "(%_cpu*)")
    else:
        if as_pointer:
            if G.pointers_with_offset:
                return get_int32_to_ptr("(" + param.value + ")", "i8", nullable_pointer)
            else:
                return "inttoptr (i32 (" + param.value + ") to i8*)"
        else:
            return param.value


def get_parameter_read_addr(param, ptrtype):
    if param.type == "externaddr":
        if _extern_is_direct(param):
            if param.ivalue == 0:
                if ptrtype == "i8":
                    return _llref(param.value)
                else:
                    return "bitcast (i8* " + _llref(param.value) + " to " + ptrtype + "*)"
            else:
                if ptrtype == "i8":
                    return "getelementptr (" + get_load_type("i8") + " " + _llref(param.value) + ", i32 " + str(param.ivalue) + ")"
                else:
                    return "bitcast (i8* getelementptr (" + get_load_type("i8") + " " + _llref(param.value) + ", i32 " + str(param.ivalue) + ") to " + ptrtype + "*)"
        else:
            addrlabel = G.dlabel_list[param.value]

            if ptrtype == "i8":
                return _dseg_gep(addrlabel, _s32(addrlabel.offset + param.ivalue))
            else:
                return "bitcast (i8* " + _dseg_gep(addrlabel, _s32(addrlabel.offset + param.ivalue)) + " to " + ptrtype + "*)"
    else:
        reg = get_parameter_read_value(param)
        return get_int32_to_ptr(reg, ptrtype)


def store_temporary_register_to_reg(temp_reg, reg, last_reg_write):
    if is_temporary_register(reg):
        if G.procedure_optimization_level >= 2:
            G.current_temporary_register[reg] = temp_reg
        else:
            add_output_line("store i32 " + temp_reg + ", i32* %" + reg + ", align 4")
    else:
        regnumstr = G.register_numbers_str[reg]

        if G.procedure_optimization_level >= 1:
            regnum = int(regnumstr)

            G.register_state[regnum] = WRITE
            if G.procedure_optimization_level >= 2:
                G.current_temporary_register[reg] = temp_reg
            else:
                add_output_line("store i32 " + temp_reg + ", i32* %" + reg + ", align 4")

            if last_reg_write:
                G.register_state[regnum] = READ

                addr = get_new_temporary_register()
                add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + regnumstr)
                add_output_line("store i32 " + temp_reg + ", i32* " + addr + ", align 4")
        else:
            addr = get_new_temporary_register()
            add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + regnumstr)
            add_output_line("store i32 " + temp_reg + ", i32* " + addr + ", align 4")


def get_float_reg_addr(top, index):
    if index == 0:
        addr = get_new_temporary_register()

        add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 12, i32 " + top)
    else:
        tmp1 = get_new_temporary_register()
        tmp2 = get_new_temporary_register()
        addr = get_new_temporary_register()

        add_output_line(tmp1 + " = add i32 " + top + ", " + str(index))
        add_output_line(tmp2 + " = and i32 " + tmp1 + ", 7")
        add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 12, i32 " + tmp2)

    return addr


def get_float_reg_value(top, index, float_stack_top):
    if G.procedure_optimization_level >= 1:
        stack_index = (index + float_stack_top) & 7

        if G.float_reg_state[stack_index] == EMPTY:
            G.float_reg_state[stack_index] = READ

            addr = get_float_reg_addr(top, index)
            reg = get_new_temporary_register()
            add_output_line(reg + " = load " + get_load_type("double") + " " + addr + ", align 4")

            if G.procedure_optimization_level >= 2:
                G.current_temporary_float_register[stack_index] = reg
            else:
                add_output_line("store double " + reg + ", double* %st" + str(stack_index) + ", align 8")
        else:
            if G.procedure_optimization_level >= 2:
                reg = G.current_temporary_float_register[stack_index]
            else:
                reg = get_new_temporary_register()
                add_output_line(reg + " = load " + get_load_type("double") + " %st" + str(stack_index) + ", align 8")
    else:
        addr = get_float_reg_addr(top, index)
        reg = get_new_temporary_register()

        add_output_line(reg + " = load " + get_load_type("double") + " " + addr + ", align 4")

    return reg


def store_temporary_register_to_float_reg(temp_reg, top, index, float_stack_top, last_float_reg_write):
    if G.procedure_optimization_level >= 1:
        stack_index = (index + float_stack_top) & 7

        G.float_reg_state[stack_index] = WRITE
        if G.procedure_optimization_level >= 2:
            G.current_temporary_float_register[stack_index] = temp_reg
        else:
            add_output_line("store double " + temp_reg + ", double* %st" + str(stack_index) + ", align 8")

        if last_float_reg_write:
            G.float_reg_state[stack_index] = READ

            addr = get_float_reg_addr(top, index)

            add_output_line("store double " + temp_reg + ", double* " + addr + ", align 4")
    else:
        addr = get_float_reg_addr(top, index)

        add_output_line("store double " + temp_reg + ", double* " + addr + ", align 4")


def increase_float_stack_top(top, value, last_reg_write, float_stack_top):
    if G.procedure_optimization_level >= 1:
        for i in range(1, value + 1):
            G.float_reg_state[(float_stack_top + i - 1) & 7] = EMPTY

    tmp = get_new_temporary_register()
    dst = get_new_temporary_register()

    add_output_line(tmp + " = add i32 " + top + ", " + str(value))
    add_output_line(dst + " = and i32 " + tmp + ", 7")
    store_temporary_register_to_reg(dst, "st_top", last_reg_write)


def binary_float_instr(instr, paramtype, reverse_param, reverse_result, pop, paramvals0, last_reg_write0, last_reg_write1, float_stack_top, last_float_reg_write0):
    top = get_parameter_read_value(Param("reg", "st_top"))
    src1 = get_float_reg_value(top, 0, float_stack_top)

    if paramtype == 1:   # INT32 reg/const
        index = 0
        isrc = get_parameter_read_value(paramvals0)
        src2 = get_new_temporary_register()
        add_output_line(src2 + " = sitofp i32 " + isrc + " to double")
    elif paramtype == 2:   # FLOAT reg/const
        index = 0
        isrc = get_parameter_read_value(paramvals0)
        tmp = get_new_temporary_register()
        src2 = get_new_temporary_register()
        add_output_line(tmp + " = bitcast i32 " + isrc + " to float")
        add_output_line(src2 + " = fpext float " + tmp + " to double")
    else:
        index = paramvals0.ivalue
        src2 = get_float_reg_value(top, index, float_stack_top)

    dst = get_new_temporary_register()

    add_output_line(dst + " = " + instr + " double " + (src2 if reverse_param else src1) + ", " + (src1 if reverse_param else src2))

    store_temporary_register_to_float_reg(dst, top, (index if reverse_result else 0), float_stack_top, last_float_reg_write0)

    if pop:
        increase_float_stack_top(top, 1, last_reg_write1, float_stack_top)

    store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write0)


def compare_float_instr(paramtype, pop, paramvals0, last_reg_write0, last_reg_write1, float_stack_top):
    top = get_parameter_read_value(Param("reg", "st_top"))
    src1 = get_float_reg_value(top, 0, float_stack_top)

    if paramtype == 2:   # FLOAT reg/const
        isrc = get_parameter_read_value(paramvals0)
        tmp1 = get_new_temporary_register()
        src2 = get_new_temporary_register()
        add_output_line(tmp1 + " = bitcast i32 " + isrc + " to float")
        add_output_line(src2 + " = fpext float " + tmp1 + " to double")
    else:
        index = paramvals0.ivalue
        src2 = get_float_reg_value(top, index, float_stack_top)

    tmp2 = get_new_temporary_register()
    tmp3 = get_new_temporary_register()
    tmp4 = get_new_temporary_register()
    dst = get_new_temporary_register()

    add_output_line(tmp2 + " = fcmp olt double " + src1 + ", " + src2)
    add_output_line(tmp3 + " = fcmp oeq double " + src1 + ", " + src2)
    add_output_line(tmp4 + " = select i1 " + tmp3 + ", i32 " + str(0x4000) + ", i32 0")
    add_output_line(dst + " = select i1 " + tmp2 + ", i32 " + str(0x0100) + ", i32 " + tmp4)

    store_temporary_register_to_reg(dst, "st_sw_cond", last_reg_write0)

    if pop > 0:
        increase_float_stack_top(top, pop, last_reg_write1, float_stack_top)


def writeback_register(regnum):
    if G.procedure_optimization_level >= 2:
        reg = G.current_temporary_register[registers_base_list[regnum]]
    else:
        reg = get_new_temporary_register()
        add_output_line(reg + " = load " + get_load_type("i32") + " %" + registers_base_list[regnum] + ", align 4")

    addr = get_new_temporary_register()
    add_output_line(addr + " = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + str(regnum))
    add_output_line("store i32 " + reg + ", i32* " + addr + ", align 4")

    G.register_state[regnum] = READ


def writeback_registers():
    for i in range(NUM_REGS):
        if G.register_state[i] == WRITE:
            writeback_register(i)


def writeback_float_register(stack_index, top, float_stack_top):
    if G.procedure_optimization_level >= 2:
        reg = G.current_temporary_float_register[stack_index]
    else:
        reg = get_new_temporary_register()
        add_output_line(reg + " = load " + get_load_type("double") + " %st" + str(stack_index) + ", align 8")

    addr = get_float_reg_addr(top, (stack_index + 8 - float_stack_top) & 7)

    add_output_line("store double " + reg + ", double* " + addr + ", align 4")

    G.float_reg_state[stack_index] = READ


def writeback_float_registers(float_stack_top):
    top = ""

    for i in range(NUM_FLOAT_REGS):
        if G.float_reg_state[i] == WRITE:
            if top == "":
                top = get_parameter_read_value(Param("reg", "st_top"))
            writeback_float_register(i, top, float_stack_top)


def _s32(v):
    v &= 0xFFFFFFFF
    if v >= 0x80000000:
        v -= 0x100000000
    return v


_ONE_REG_WRITE0 = ("mov", "add", "sub", "shl", "lshr", "ashr", "and", "or", "xor", "mul",
                   "load", "load8s", "load8z", "load16s", "load16z",
                   "ext8s", "ext16s", "ins16", "ins8ll", "ins8lh", "ins8hl", "ins8hh",
                   "ctlz", "bswap")
_FLOAT_WRITE_TOP0 = ("FCHS_VOID", "FIADD_INT32", "FADD_FLOAT", "FISUB_INT32", "FISUBR_INT32",
                     "FSUB_FLOAT", "FSUBR_FLOAT", "FIMUL_INT32", "FMUL_FLOAT", "FDIV_FLOAT",
                     "FDIVR_FLOAT", "FIDIV_INT32", "FIDIVR_INT32", "FSQRT_VOID", "FSIN_VOID",
                     "FCOS_VOID", "FLOG_VOID", "FLOG10_VOID", "FABS_VOID", "FROUND_VOID")
_BINARY_FLOAT = {
    # word: (instr, paramtype, reverse_param, reverse_result, pop)
    "FIADD_INT32": ("fadd", 1, False, False, False),
    "FADD_FLOAT": ("fadd", 2, False, False, False),
    "FADD_ST": ("fadd", 0, False, False, False),
    "FADD_TO_ST": ("fadd", 0, True, True, False),
    "FADDP_ST": ("fadd", 0, True, True, True),
    "FISUB_INT32": ("fsub", 1, False, False, False),
    "FISUBR_INT32": ("fsub", 1, True, False, False),
    "FSUB_FLOAT": ("fsub", 2, False, False, False),
    "FSUB_ST": ("fsub", 0, False, False, False),
    "FSUB_TO_ST": ("fsub", 0, True, True, False),
    "FSUBP_ST": ("fsub", 0, True, True, True),
    "FSUBR_FLOAT": ("fsub", 2, True, False, False),
    "FSUBR_ST": ("fsub", 0, True, False, False),
    "FSUBRP_ST": ("fsub", 0, False, True, True),
    "FIMUL_INT32": ("fmul", 1, False, False, False),
    "FMUL_FLOAT": ("fmul", 2, False, False, False),
    "FMUL_ST": ("fmul", 0, False, False, False),
    "FMUL_TO_ST": ("fmul", 0, True, True, False),
    "FMULP_ST": ("fmul", 0, True, True, True),
    "FDIV_FLOAT": ("fdiv", 2, False, False, False),
    "FDIV_ST": ("fdiv", 0, False, False, False),
    "FDIV_TO_ST": ("fdiv", 0, True, True, False),
    "FDIVP_ST": ("fdiv", 0, True, True, True),
    "FDIVR_FLOAT": ("fdiv", 2, True, False, False),
    "FDIVR_ST": ("fdiv", 0, True, False, False),
    "FDIVRP_ST": ("fdiv", 0, False, True, True),
    "FIDIV_INT32": ("fdiv", 1, False, False, False),
    "FIDIVR_INT32": ("fdiv", 1, True, False, False),
}
_COMPARE_FLOAT = {
    # word: (paramtype, pop)
    "FCOM_FLOAT": (2, 0),
    "FCOM_ST": (0, 0),
    "FUCOM_ST": (0, 0),
    "FCOMP_FLOAT": (2, 1),
    "FCOMP_ST": (0, 1),
    "FUCOMP_ST": (0, 1),
}
_FLOAT_UNARY = {
    "FSQRT_VOID": "sqrt",
    "FSIN_VOID": "sin",
    "FCOS_VOID": "cos",
    "FLOG_VOID": "log",
    "FLOG10_VOID": "log10",
    "FABS_VOID": "fabs",
    "FROUND_VOID": "round",
}


def process_proc_body(proc_name):
    G.current_proc = proc_name
    curproc = G.proc_list[proc_name]
    is_inside_if = False
    is_static_if = False
    static_if_result = False
    if_contains_call = False
    current_float_stack_top = 0
    if_float_stack_top = 0
    endif_label = ""
    if_reg_state = [EMPTY] * NUM_REGS
    if_float_reg_state = [EMPTY] * NUM_FLOAT_REGS
    if_reg64_state = [EMPTY] * NUM_REGS64
    used_reg_list = {}
    used_float_reg_list = {}
    used_reg64_list = {}

    if len(curproc.lines) == 0:
        write_error2("Empty procedure")
        return False

    proc_instr_info = [ProcInstr() for _ in curproc.lines]

    G.procedure_optimization_level = G.global_optimization_level

    # ------------------------------------------------------------------
    # analyze / check pass

    current_float_stack_top = 0

    if G.pointers_with_offset:
        used_reg64_list["ptrofs"] = True

    for linenum in range(len(curproc.lines)):
        G.current_line = curproc.lines[linenum]
        current_line = G.current_line
        instruction = G.instructions_list[current_line.word]
        info = proc_instr_info[linenum]

        info.is_valid = True
        info.is_inside_if = False
        info.func_has_return_value = False
        info.if_contains_call = False
        info.if_changes_st_top = False
        info.float_stack_top = current_float_stack_top
        info.write_reg = ["", "", ""]
        info.write_float_reg = [-1, -1]
        info.last_reg_write = [False, False, False]
        info.last_float_reg_write = [False, False]
        info.params = []

        if len(instruction.variable_params2) != 0:
            if current_line.word == "call" and len(instruction.params) == 1:
                if current_line.param1 not in G.func_list:
                    write_error2("Function not found: " + current_line.param1)
                    return False

                func = G.func_list[current_line.param1]

                info.func_has_return_value = func.has_return_value

                params = get_current_params2()

                if len(params) != len(func.params):
                    write_error2("Wrong number of function parameters: " + current_line.orig)
                    return False

                for i in range(len(params)):
                    if func.params[i].type == 2:
                        if params[i] != "$":
                            write_error2("Expecting $ in function parameters: " + current_line.orig)
                            return False
                    else:
                        if params[i] == "$":
                            write_error2("Unexpected $ in function parameters: " + current_line.orig)
                            return False
            else:
                write_error2("Instruction error: " + current_line.orig)
                return False
        else:
            params = get_current_params()

        paramvals = [None] * len(params)

        # get parameters type and value
        for i in range(len(params)):
            if params[i] == "$":
                paramvals[i] = Param("$", "%cpu")
                continue

            if params[i] in G.registers_list:
                paramvals[i] = Param("reg", params[i])

                # update list of used registers in procedure
                used_reg_list[params[i]] = True

                continue

            if params[i] in G.func_list:
                paramvals[i] = Param("funcaddr", G.func_list[params[i]].real_name)
                continue

            position = params[i].find('[')
            if position >= 0:
                displacement = strip(params[i][position + 1:])
                if len(displacement) > 0 and displacement[-1] == ']':
                    displacement = strip(displacement[0:-1])
                    param_base = strip(params[i][0:position])

                    if param_base in G.define_list:
                        param_base = G.define_list[param_base]

                    if param_base in G.extern_list or param_base in G.dlabel_list:
                        ok, displacement = replace_defines_in_expr(displacement)
                        if ok:
                            ok, displacement = calculate_expr_value(displacement)
                            if ok:
                                paramvals[i] = Param("externaddr", param_base, _s32(_to_long(displacement)))
                                continue

            if params[i] in G.define_list:
                param = G.define_list[params[i]]
            else:
                param = params[i]

            if param in G.extern_list or param in G.dlabel_list:
                paramvals[i] = Param("externaddr", param, 0)
                continue

            if param in G.proc_list:
                paramvals[i] = Param("procaddr", param)
                continue

            ok, param = replace_defines_in_expr(param)
            if ok:
                ok, param = calculate_expr_value(param)
                if ok:
                    paramvals[i] = Param("const", param, _s32(_to_long(param)))
                    continue

            write_error2("Unknown instruction parameter: " + current_line.orig)
            return False

        # check parameters type
        if len(instruction.variable_params2) != 0:
            for pv in paramvals:
                if pv.type not in instruction.variable_params2:
                    write_error2("Wrong instruction parameter type: " + current_line.orig)
                    return False
        else:
            for i in range(len(paramvals)):
                if paramvals[i].type not in instruction.params2[i]:
                    write_error2("Wrong instruction parameter type: " + current_line.orig)
                    return False

        info.params = list(paramvals)

        if is_inside_if:
            if is_static_if:
                if not static_if_result:
                    info.is_valid = False
            else:
                info.is_inside_if = True

        word = current_line.word

        if word in _ONE_REG_WRITE0:
            info.write_reg[0] = paramvals[0].value
        elif word in ("store", "store8", "store16"):
            pass
        elif word == "tcall":
            if linenum + 1 != len(curproc.lines):
                write_error2("Instructions after tcall: " + current_line.orig)
                return False
        elif word in ("ctcallz", "ctcallnz"):
            if linenum + 2 != len(curproc.lines) or curproc.lines[linenum + 1].word != "tcall":
                write_error2("Instructions after ctcall: " + current_line.orig)
                return False
        elif word == "cmovz":
            info.write_reg[0] = paramvals[1].value
        elif word in ("cmoveq", "cmovult", "cmovugt", "cmovslt", "cmovsgt"):
            info.write_reg[0] = paramvals[2].value
        elif word == "call":
            if info.func_has_return_value:
                info.write_reg[0] = "tmp0"
            if is_inside_if:
                if_contains_call = True
        elif word in ("imul", "umul"):
            info.write_reg[0] = paramvals[0].value
            info.write_reg[1] = paramvals[1].value
        elif word in ("ifz", "ifnz"):
            if is_inside_if:
                write_error2("If inside if: " + current_line.orig)
                return False

            is_inside_if = True
            is_static_if = False
            if_contains_call = False
            if_float_stack_top = current_float_stack_top

            if G.procedure_optimization_level >= 1:
                if linenum > 0 and curproc.lines[linenum - 1].word == "mov":
                    prev = proc_instr_info[linenum - 1]
                    if prev.params[0].value == paramvals[0].value and prev.params[1].type == "const":
                        is_static_if = True
                        value = prev.params[1].ivalue

                        if current_line.word == "ifz":
                            static_if_result = (value == 0)
                        else:
                            static_if_result = (value != 0)

                if linenum > 1 and curproc.lines[linenum - 2].word == "mov" and curproc.lines[linenum - 1].word == "cmovslt":
                    prev2 = proc_instr_info[linenum - 2]
                    prev1 = proc_instr_info[linenum - 1]
                    if prev2.params[0].value == paramvals[0].value and prev2.params[1].type == "const":
                        if (prev1.params[0].value == paramvals[0].value and
                                prev1.params[1].type == "const" and
                                prev1.params[2].value == paramvals[0].value and
                                prev1.params[3].type == "const" and
                                prev1.params[4].type == "const"):
                            is_static_if = True
                            value2 = prev2.params[1].ivalue
                            value1 = prev1.params[1].ivalue

                            if value2 < value1:
                                value0 = prev1.params[3].ivalue
                            else:
                                value0 = prev1.params[4].ivalue

                            if current_line.word == "ifz":
                                static_if_result = (value0 == 0)
                            else:
                                static_if_result = (value0 != 0)

            if G.procedure_optimization_level > 1 and not is_static_if:
                G.procedure_optimization_level = 1

            if is_static_if:
                info.is_valid = False
        elif word == "endif":
            if not is_inside_if:
                write_error2("Endif without if: " + current_line.orig)
                return False

            is_inside_if = False
            info.is_inside_if = False
            info.if_contains_call = if_contains_call
            info.if_changes_st_top = (current_float_stack_top != if_float_stack_top)

            if is_static_if:
                info.is_valid = False

                if not static_if_result:
                    current_float_stack_top = if_float_stack_top
        elif word in ("DIV_64_ZE", "IDIV_64_SE"):
            info.write_reg[0] = "eax"
            info.write_reg[1] = "edx"
        elif word == "FLDCW_UINT16":
            info.write_reg[0] = "st_cw"
        elif word == "FNSTCW_VOID":
            info.write_reg[0] = "tmp0"
            used_reg_list["st_cw"] = True
        elif word == "FNSTSW_VOID":
            info.write_reg[0] = "tmp0"
            used_reg_list["st_sw_cond"] = True
            used_reg_list["st_top"] = True
        elif word in _FLOAT_WRITE_TOP0:
            info.write_reg[0] = "st_sw_cond"
            info.write_float_reg[0] = current_float_stack_top
            used_reg_list["st_top"] = True
        elif word == "FTAN_VOID":
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            info.write_float_reg[0] = current_float_stack_top
            current_float_stack_top = (current_float_stack_top + 7) & 7
            info.write_float_reg[1] = current_float_stack_top
        elif word == "FXCH_ST":
            info.write_reg[0] = "st_sw_cond"
            info.write_float_reg[0] = current_float_stack_top
            info.write_float_reg[1] = (current_float_stack_top + paramvals[0].ivalue) & 7
            used_reg_list["st_top"] = True
        elif word in ("FST_ST", "FADD_TO_ST", "FSUB_TO_ST", "FMUL_TO_ST", "FDIV_TO_ST"):
            info.write_reg[0] = "st_sw_cond"
            info.write_float_reg[0] = (current_float_stack_top + paramvals[0].ivalue) & 7
            used_reg_list["st_top"] = True
            used_float_reg_list[current_float_stack_top] = True
        elif word in ("FADD_ST", "FSUB_ST", "FSUBR_ST", "FMUL_ST", "FDIV_ST", "FDIVR_ST"):
            info.write_reg[0] = "st_sw_cond"
            info.write_float_reg[0] = current_float_stack_top
            used_reg_list["st_top"] = True
            used_float_reg_list[(current_float_stack_top + paramvals[0].ivalue) & 7] = True
        elif word == "FCOM_FLOAT":
            info.write_reg[0] = "st_sw_cond"
            used_reg_list["st_top"] = True
            used_float_reg_list[current_float_stack_top] = True
        elif word in ("FCOM_ST", "FUCOM_ST"):
            info.write_reg[0] = "st_sw_cond"
            used_reg_list["st_top"] = True
            used_float_reg_list[current_float_stack_top] = True
            used_float_reg_list[(current_float_stack_top + paramvals[0].ivalue) & 7] = True
        elif word in ("FILD_INT32", "FLD_FLOAT", "FLD1_VOID", "FLDLG2_VOID", "FLDLN2_VOID", "FLDZ_VOID"):
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            current_float_stack_top = (current_float_stack_top + 7) & 7
            info.write_float_reg[0] = current_float_stack_top
        elif word == "FLD_ST":
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            used_float_reg_list[(current_float_stack_top + paramvals[0].ivalue) & 7] = True

            current_float_stack_top = (current_float_stack_top + 7) & 7
            info.write_float_reg[0] = current_float_stack_top
        elif word in ("FSTP_ST", "FADDP_ST", "FSUBP_ST", "FSUBRP_ST", "FMULP_ST", "FDIVP_ST", "FDIVRP_ST"):
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            info.write_float_reg[0] = (current_float_stack_top + paramvals[0].ivalue) & 7
            used_float_reg_list[current_float_stack_top] = True

            current_float_stack_top = (current_float_stack_top + 1) & 7
        elif word == "FCOMP_FLOAT":
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            used_float_reg_list[current_float_stack_top] = True

            current_float_stack_top = (current_float_stack_top + 1) & 7
        elif word in ("FCOMP_ST", "FUCOMP_ST"):
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            used_float_reg_list[current_float_stack_top] = True
            used_float_reg_list[(current_float_stack_top + paramvals[0].ivalue) & 7] = True

            current_float_stack_top = (current_float_stack_top + 1) & 7
        elif word in ("FMOD_VOID", "FMODR_VOID", "FPOW_VOID", "FPOWR_VOID", "FYL2X_VOID"):
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            info.write_float_reg[0] = (current_float_stack_top + 1) & 7
            used_float_reg_list[current_float_stack_top] = True

            current_float_stack_top = (current_float_stack_top + 1) & 7
        elif word in ("FCOMPP_VOID", "FUCOMPP_VOID"):
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[1] = "st_top"
            used_float_reg_list[current_float_stack_top] = True
            used_float_reg_list[(current_float_stack_top + 1) & 7] = True

            current_float_stack_top = (current_float_stack_top + 2) & 7
        elif word in ("FST_FLOAT", "FSTP_FLOAT"):
            info.write_reg[0] = "st_sw_cond"
            info.write_reg[2] = "tmp0"
            used_reg_list["st_top"] = True
            used_float_reg_list[current_float_stack_top] = True

            if current_line.word == "FSTP_FLOAT":
                info.write_reg[1] = "st_top"
                current_float_stack_top = (current_float_stack_top + 1) & 7
        else:
            write_error2("Unhandled instruction: " + current_line.orig)
            return False

        # update list of used registers in procedure
        for i in range(3):
            reg = info.write_reg[i]
            if reg != "":
                used_reg_list[reg] = True

        # update list of used float registers in procedure
        for i in range(2):
            reg = info.write_float_reg[i]
            if reg >= 0:
                used_float_reg_list[reg] = True

    if is_inside_if:
        write_error2("Missing endif: " + G.current_line.orig)
        return False

    # ------------------------------------------------------------------
    # optimization pass

    if G.procedure_optimization_level >= 1:
        if_changes_st_top = False
        last_reg_write = {}
        last_float_reg_write = [True] * NUM_FLOAT_REGS

        for reg in registers_base_list:
            last_reg_write[reg] = True

        for linenum in range(len(curproc.lines) - 1, -1, -1):
            info = proc_instr_info[linenum]
            if not info.is_valid:
                continue

            instr_word = curproc.lines[linenum].word

            if is_inside_if:
                if instr_word == "ifz" or instr_word == "ifnz":
                    is_inside_if = False
                    info.if_contains_call = if_contains_call
                    info.if_changes_st_top = if_changes_st_top
            else:
                if instr_word == "endif":
                    is_inside_if = True
                    if_contains_call = info.if_contains_call
                    if_changes_st_top = info.if_changes_st_top

            for i in range(3):
                reg = info.write_reg[i]

                if reg != "":
                    if last_reg_write[reg]:
                        if not info.is_inside_if:
                            info.last_reg_write[i] = True

                    last_reg_write[reg] = False
            for i in range(2):
                stack_index = info.write_float_reg[i]

                if stack_index >= 0:
                    if last_float_reg_write[stack_index]:
                        if not info.is_inside_if:
                            info.last_float_reg_write[i] = True

                    last_float_reg_write[stack_index] = False

            if instr_word == "call" or ((instr_word == "ifz" or instr_word == "ifnz") and info.if_contains_call):
                for reg in registers_base_list:
                    last_reg_write[reg] = True
            if instr_word == "call" or ((instr_word == "ifz" or instr_word == "ifnz") and (info.if_contains_call or info.if_changes_st_top)):
                for i in range(NUM_FLOAT_REGS):
                    last_float_reg_write[i] = True

    # ------------------------------------------------------------------
    # output pass

    G.temporary_register_index = 0
    G.label_index = 0
    G.current_temporary_register = {}
    G.current_temporary_float_register = {}
    G.current_temporary_register64 = {}

    if G.procedure_optimization_level >= 1:
        for i in range(NUM_REGS):
            G.register_state[i] = EMPTY
            if G.procedure_optimization_level == 1 and registers_base_list[i] in used_reg_list:
                add_output_line("%" + registers_base_list[i] + " = alloca i32, align 4")

        for i in range(NUM_FLOAT_REGS):
            G.float_reg_state[i] = EMPTY
            if G.procedure_optimization_level == 1 and i in used_float_reg_list:
                add_output_line("%st" + str(i) + " = alloca double, align 8")

        for i in range(NUM_REGS64):
            G.register64_state[i] = EMPTY
            if G.procedure_optimization_level == 1 and registers64_base_list[i] in used_reg64_list:
                add_output_line("%" + registers64_base_list[i] + " = alloca i64, align 8")

    if G.procedure_optimization_level <= 1:
        for i in range(NUM_REGS, len(registers_base_list)):
            if registers_base_list[i] in used_reg_list:
                add_output_line("%" + registers_base_list[i] + " = alloca i32, align 4")

    for linenum in range(len(curproc.lines)):
        G.current_line = curproc.lines[linenum]
        current_line = G.current_line
        word = current_line.word

        add_output_line(";" + current_line.orig)
        if not proc_instr_info[linenum].is_valid:
            continue

        paramvals = proc_instr_info[linenum].params
        last_reg_write = proc_instr_info[linenum].last_reg_write
        last_float_reg_write = proc_instr_info[linenum].last_float_reg_write
        current_float_stack_top = proc_instr_info[linenum].float_stack_top

        if word == "mov":
            src = get_parameter_read_value(paramvals[1])
            store_temporary_register_to_reg(src, paramvals[0].value, last_reg_write[0])
        elif word in ("add", "sub", "shl", "lshr", "ashr", "and", "or", "xor", "mul"):
            src1 = get_parameter_read_value(paramvals[1])
            src2 = get_parameter_read_value(paramvals[2])
            dst = get_new_temporary_register()

            add_output_line(dst + " = " + current_line.word + " i32 " + src1 + ", " + src2)

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word == "load":
            src = get_parameter_read_addr(paramvals[1], "i32")
            dst = get_new_temporary_register()

            add_output_line(dst + " = load " + get_load_type("i32") + " " + src + ", align " + paramvals[2].value)

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word in ("load8s", "load8z", "load16s", "load16z"):
            srctype = "i8" if (word == "load8s" or word == "load8z") else "i16"
            extinstr = "sext" if (word == "load8s" or word == "load16s") else "zext"

            src = get_parameter_read_addr(paramvals[1], srctype)
            tmpdst = get_new_temporary_register()
            dst = get_new_temporary_register()

            add_output_line(tmpdst + " = load " + get_load_type(srctype) + " " + src + ", align " + paramvals[2].value)
            add_output_line(dst + " = " + extinstr + " " + srctype + " " + tmpdst + " to i32")

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word == "store":
            src = get_parameter_read_value(paramvals[0])
            dst = get_parameter_read_addr(paramvals[1], "i32")

            add_output_line("store i32 " + src + ", i32* " + dst + ", align " + paramvals[2].value)
        elif word in ("store8", "store16"):
            srctype = "i8" if word == "store8" else "i16"

            src = get_parameter_read_value(paramvals[0])
            dst = get_parameter_read_addr(paramvals[1], srctype)
            src2 = get_new_temporary_register()

            add_output_line(src2 + " = trunc i32 " + src + " to " + srctype)
            add_output_line("store " + srctype + " " + src2 + ", " + srctype + "* " + dst + ", align " + paramvals[2].value)
        elif word in ("ext8s", "ext16s"):
            src = get_parameter_read_value(paramvals[1])
            src2 = get_new_temporary_register()
            dst = get_new_temporary_register()

            srctype = "i8" if word == "ext8s" else "i16"

            add_output_line(src2 + " = trunc i32 " + src + " to " + srctype)
            add_output_line(dst + " = sext " + srctype + " " + src2 + " to i32")

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word in ("ins16", "ins8ll"):
            src1 = get_parameter_read_value(paramvals[1])
            src2 = get_parameter_read_value(paramvals[2])
            src21 = get_new_temporary_register()
            src22 = get_new_temporary_register()
            src11 = get_new_temporary_register()
            dst = get_new_temporary_register()

            srctype = "i16" if word == "ins16" else "i8"
            andvalue = 0xffff0000 if word == "ins16" else 0xffffff00

            add_output_line(src21 + " = trunc i32 " + src2 + " to " + srctype)
            add_output_line(src22 + " = zext " + srctype + " " + src21 + " to i32")
            add_output_line(src11 + " = and i32 " + src1 + ", " + str(andvalue))
            add_output_line(dst + " = or i32 " + src11 + ", " + src22)

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word in ("ins8lh", "ins8hl"):
            src1 = get_parameter_read_value(paramvals[1])
            src2 = get_parameter_read_value(paramvals[2])
            src21 = get_new_temporary_register()
            src22 = get_new_temporary_register()
            src23 = get_new_temporary_register()
            src11 = get_new_temporary_register()
            dst = get_new_temporary_register()

            if word == "ins8lh":
                add_output_line(src21 + " = lshr i32 " + src2 + ", 8")
                add_output_line(src22 + " = trunc i32 " + src21 + " to i8")
                add_output_line(src23 + " = zext i8 " + src22 + " to i32")
                add_output_line(src11 + " = and i32 " + src1 + ", " + str(0xffffff00))
            else:
                add_output_line(src21 + " = trunc i32 " + src2 + " to i8")
                add_output_line(src22 + " = zext i8 " + src21 + " to i32")
                add_output_line(src23 + " = shl i32 " + src22 + ", 8")
                add_output_line(src11 + " = and i32 " + src1 + ", " + str(0xffff00ff))

            add_output_line(dst + " = or i32 " + src11 + ", " + src23)

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word == "ins8hh":
            src1 = get_parameter_read_value(paramvals[1])
            src2 = get_parameter_read_value(paramvals[2])
            src21 = get_new_temporary_register()
            src11 = get_new_temporary_register()
            dst = get_new_temporary_register()

            add_output_line(src21 + " = and i32 " + src2 + ", " + str(0xff00))
            add_output_line(src11 + " = and i32 " + src1 + ", " + str(0xffff00ff))
            add_output_line(dst + " = or i32 " + src11 + ", " + src21)

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word == "tcall":
            if G.procedure_optimization_level >= 1:
                writeback_registers()
                writeback_float_registers(current_float_stack_top)

            if G.no_tail_calls:
                addr = get_parameter_read_value(paramvals[0], True)
                add_output_line("ret i8* " + addr)
            else:
                if paramvals[0].type == "procaddr":
                    add_output_line("musttail call fastcc void " + _llref(paramvals[0].value) + "(%_cpu* %cpu) nounwind")
                else:
                    dst = get_parameter_read_value(paramvals[0])
                    addr = get_int32_to_ptr(dst, "void(%_cpu*)")

                    add_output_line("musttail call fastcc void " + addr + "(%_cpu* %cpu) nounwind")
        elif word in ("ctcallz", "ctcallnz"):
            if G.procedure_optimization_level >= 1:
                writeback_registers()
                writeback_float_registers(current_float_stack_top)

            condreg = get_parameter_read_value(paramvals[0])
            cond = get_new_temporary_register()
            labelt = get_new_label()
            labelf = get_new_label()

            condtype = "eq" if word == "ctcallz" else "ne"

            add_output_line(cond + " = icmp " + condtype + " i32 " + condreg + ", 0")
            add_output_line("br i1 " + cond + ", label %" + labelt + ", label %" + labelf)
            add_output_line(labelt + ":")

            if G.no_tail_calls:
                addr = get_parameter_read_value(paramvals[1], True)
                add_output_line("ret i8* " + addr)
            else:
                add_output_line("musttail call fastcc void " + _llref(paramvals[1].value) + "(%_cpu* %cpu) nounwind")

                add_output_line("ret void")
            add_output_line(labelf + ":")
        elif word == "cmovz":
            condreg = get_parameter_read_value(paramvals[0])
            src1 = get_parameter_read_value(paramvals[2])
            src2 = get_parameter_read_value(paramvals[3])
            cond = get_new_temporary_register()
            dst = get_new_temporary_register()

            add_output_line(cond + " = icmp eq i32 " + condreg + ", 0")
            add_output_line(dst + " = select i1 " + cond + ", i32 " + src1 + ", i32 " + src2)

            store_temporary_register_to_reg(dst, paramvals[1].value, last_reg_write[0])
        elif word in ("cmoveq", "cmovult", "cmovugt", "cmovslt", "cmovsgt"):
            condreg1 = get_parameter_read_value(paramvals[0])
            condreg2 = get_parameter_read_value(paramvals[1])
            src1 = get_parameter_read_value(paramvals[3])
            src2 = get_parameter_read_value(paramvals[4])
            cond = get_new_temporary_register()
            dst = get_new_temporary_register()

            condtype = word[4:]

            add_output_line(cond + " = icmp " + condtype + " i32 " + condreg1 + ", " + condreg2)
            add_output_line(dst + " = select i1 " + cond + ", i32 " + src1 + ", i32 " + src2)

            store_temporary_register_to_reg(dst, paramvals[2].value, last_reg_write[0])
        elif word == "call":
            if G.procedure_optimization_level >= 1:
                writeback_registers()
                writeback_float_registers(current_float_stack_top)

            func = G.func_list[current_line.param1]

            src = [None] * len(paramvals)
            for i in range(len(paramvals)):
                src[i] = get_parameter_read_value(paramvals[i], (func.params[i].type == 1), True)

            srcstr = ""
            for i in range(len(src)):
                if func.params[i].type == 2:
                    datatype = "%_cpu*"
                elif func.params[i].type == 1:
                    datatype = "i8*"
                else:
                    datatype = "i32"

                if i > 0:
                    srcstr += ", "
                srcstr = srcstr + datatype + " " + src[i]

            if G.procedure_optimization_level >= 1:
                for i in range(NUM_REGS):
                    G.register_state[i] = EMPTY
                G.current_temporary_register = {}

                for i in range(NUM_FLOAT_REGS):
                    G.float_reg_state[i] = EMPTY
                G.current_temporary_float_register = {}

                for i in range(NUM_REGS64):
                    G.register64_state[i] = EMPTY
                G.current_temporary_register64 = {}

            if func.has_return_value:
                dst = get_new_temporary_register()

                add_output_line(dst + " = call " + ("i8*" if func.return_value_is_pointer else "i32") + " " + _llref(func.real_name) + "(" + srcstr + ") nounwind")

                if func.return_value_is_pointer:
                    if G.pointers_with_offset:
                        dst2 = get_addr_to_int32(dst, "i8", True)
                    else:
                        dst2 = get_new_temporary_register()

                        add_output_line(dst2 + " = ptrtoint i8* " + dst + " to i32")

                    store_temporary_register_to_reg(dst2, "tmp0", last_reg_write[0])
                else:
                    store_temporary_register_to_reg(dst, "tmp0", last_reg_write[0])
            else:
                add_output_line("call void " + _llref(func.real_name) + "(" + srcstr + ") nounwind")
        elif word in ("imul", "umul"):
            src1 = get_parameter_read_value(paramvals[2])
            src2 = get_parameter_read_value(paramvals[3])
            src11 = get_new_temporary_register()
            src21 = get_new_temporary_register()
            dst = get_new_temporary_register()
            dstlo = get_new_temporary_register()
            tmpdst = get_new_temporary_register()
            dsthi = get_new_temporary_register()

            extinstr = "sext" if word == "imul" else "zext"

            add_output_line(src11 + " = " + extinstr + " i32 " + src1 + " to i64")
            add_output_line(src21 + " = " + extinstr + " i32 " + src2 + " to i64")
            add_output_line(dst + " = mul i64 " + src11 + ", " + src21)
            add_output_line(dstlo + " = trunc i64 " + dst + " to i32")
            add_output_line(tmpdst + " = lshr i64 " + dst + ", 32")
            add_output_line(dsthi + " = trunc i64 " + tmpdst + " to i32")

            store_temporary_register_to_reg(dstlo, paramvals[0].value, last_reg_write[0])
            store_temporary_register_to_reg(dsthi, paramvals[1].value, last_reg_write[1])
        elif word in ("ifz", "ifnz"):
            if G.procedure_optimization_level >= 1:
                if proc_instr_info[linenum].if_contains_call:
                    writeback_registers()
                if proc_instr_info[linenum].if_contains_call or proc_instr_info[linenum].if_changes_st_top:
                    writeback_float_registers(current_float_stack_top)

            condreg = get_parameter_read_value(paramvals[0])
            cond = get_new_temporary_register()
            labelt = get_new_label()
            labelf = get_new_label()

            condtype = "eq" if word == "ifz" else "ne"

            if G.procedure_optimization_level >= 1:
                if_reg_state = list(G.register_state)
                if_float_reg_state = list(G.float_reg_state)
                if_reg64_state = list(G.register64_state)

            add_output_line(cond + " = icmp " + condtype + " i32 " + condreg + ", 0")
            add_output_line("br i1 " + cond + ", label %" + labelt + ", label %" + labelf)
            add_output_line(labelt + ":")

            endif_label = labelf
        elif word == "endif":
            if G.procedure_optimization_level >= 1:
                for i in range(NUM_REGS):
                    if G.register_state[i] == if_reg_state[i]:
                        continue

                    if G.register_state[i] == WRITE:
                        writeback_register(i)

                    if if_reg_state[i] == WRITE:
                        if G.register_state[i] == EMPTY:
                            get_parameter_read_value(Param("reg", registers_base_list[i]))
                        G.register_state[i] = WRITE

                if proc_instr_info[linenum].if_contains_call or proc_instr_info[linenum].if_changes_st_top:
                    writeback_float_registers(current_float_stack_top)

                    for i in range(NUM_FLOAT_REGS):
                        G.float_reg_state[i] = EMPTY
                    G.current_temporary_float_register = {}
                else:
                    top = ""

                    for i in range(NUM_FLOAT_REGS):
                        if G.float_reg_state[i] == if_float_reg_state[i]:
                            continue

                        if G.float_reg_state[i] == WRITE:
                            if top == "":
                                top = get_parameter_read_value(Param("reg", "st_top"))
                            writeback_float_register(i, top, current_float_stack_top)

                        if G.float_reg_state[i] != if_float_reg_state[i]:
                            G.float_reg_state[i] = EMPTY

                for i in range(NUM_REGS):
                    if G.register_state[i] != if_reg_state[i]:
                        G.register_state[i] = EMPTY

                for i in range(NUM_REGS64):
                    if G.register64_state[i] != if_reg64_state[i]:
                        G.register64_state[i] = EMPTY

            add_output_line("br label %" + endif_label)
            add_output_line(endif_label + ":")
        elif word == "ctlz":
            src = get_parameter_read_value(paramvals[1])
            dst = get_new_temporary_register()

            add_output_line(dst + " = call i32 @llvm.ctlz.i32(i32 " + src + ", i1 0)")

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word == "bswap":
            src = get_parameter_read_value(paramvals[1])
            dst = get_new_temporary_register()

            add_output_line(dst + " = call i32 @llvm.bswap.i32(i32 " + src + ")")

            store_temporary_register_to_reg(dst, paramvals[0].value, last_reg_write[0])
        elif word in ("DIV_64_ZE", "IDIV_64_SE"):
            src1 = get_parameter_read_value(Param("reg", "eax"))
            src2 = get_parameter_read_value(paramvals[0])
            dst1 = get_new_temporary_register()
            dst2 = get_new_temporary_register()

            prefix = "u" if word == "DIV_64_ZE" else "s"

            add_output_line(dst1 + " = " + prefix + "div i32 " + src1 + ", " + src2)
            add_output_line(dst2 + " = " + prefix + "rem i32 " + src1 + ", " + src2)

            store_temporary_register_to_reg(dst1, "eax", last_reg_write[0])
            store_temporary_register_to_reg(dst2, "edx", last_reg_write[1])
        elif word == "FLDCW_UINT16":
            src = get_parameter_read_value(paramvals[0])
            dst = get_new_temporary_register()

            add_output_line(dst + " = and i32 " + src + ", " + str(0xffff))

            store_temporary_register_to_reg(dst, "st_cw", last_reg_write[0])
        elif word == "FNSTCW_VOID":
            src = get_parameter_read_value(Param("reg", "st_cw"))

            store_temporary_register_to_reg(src, "tmp0", last_reg_write[0])
        elif word == "FNSTSW_VOID":
            src1 = get_parameter_read_value(Param("reg", "st_top"))
            src2 = get_parameter_read_value(Param("reg", "st_sw_cond"))
            dst1 = get_new_temporary_register()
            dst2 = get_new_temporary_register()

            add_output_line(dst1 + " = shl i32 " + src1 + ", 11")
            add_output_line(dst2 + " = add i32 " + src2 + ", " + dst1)

            store_temporary_register_to_reg(dst2, "tmp0", last_reg_write[0])
        elif word == "FCHS_VOID":
            top = get_parameter_read_value(Param("reg", "st_top"))
            src = get_float_reg_value(top, 0, current_float_stack_top)
            dst = get_new_temporary_register()

            add_output_line(dst + " = fneg double " + src)

            store_temporary_register_to_float_reg(dst, top, 0, current_float_stack_top, last_float_reg_write[0])
            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        elif word == "FXCH_ST":
            top = get_parameter_read_value(Param("reg", "st_top"))
            index = paramvals[0].ivalue
            src1 = get_float_reg_value(top, 0, current_float_stack_top)
            src2 = get_float_reg_value(top, index, current_float_stack_top)

            store_temporary_register_to_float_reg(src1, top, index, current_float_stack_top, last_float_reg_write[1])
            store_temporary_register_to_float_reg(src2, top, 0, current_float_stack_top, last_float_reg_write[0])
            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        elif word in ("FILD_INT32", "FLD_FLOAT", "FLD_ST", "FLD1_VOID", "FLDLG2_VOID", "FLDLN2_VOID", "FLDZ_VOID"):
            top1 = get_parameter_read_value(Param("reg", "st_top"))

            if word == "FILD_INT32":
                src = get_parameter_read_value(paramvals[0])
                dst = get_new_temporary_register()
                add_output_line(dst + " = sitofp i32 " + src + " to double")
            elif word == "FLD_FLOAT":
                src = get_parameter_read_value(paramvals[0])
                tmp1 = get_new_temporary_register()
                dst = get_new_temporary_register()
                add_output_line(tmp1 + " = bitcast i32 " + src + " to float")
                add_output_line(dst + " = fpext float " + tmp1 + " to double")
            elif word == "FLD_ST":
                index = paramvals[0].ivalue
                dst = get_float_reg_value(top1, index, current_float_stack_top)
            elif word == "FLD1_VOID":
                dst = "1.0"
            elif word == "FLDLG2_VOID":
                dst = "0.30102999566398119521"
            elif word == "FLDLN2_VOID":
                dst = "0.69314718055994530942"
            else:
                dst = "0.0"

            tmp2 = get_new_temporary_register()
            top2 = get_new_temporary_register()

            add_output_line(tmp2 + " = add i32 " + top1 + ", 7")
            add_output_line(top2 + " = and i32 " + tmp2 + ", 7")

            store_temporary_register_to_float_reg(dst, top2, 0, (current_float_stack_top + 7) & 7, last_float_reg_write[0])
            store_temporary_register_to_reg(top2, "st_top", last_reg_write[1])
            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        elif word in ("FST_FLOAT", "FST_ST", "FSTP_FLOAT", "FSTP_ST"):
            top = get_parameter_read_value(Param("reg", "st_top"))

            if word == "FST_ST" or word == "FSTP_ST":
                index = paramvals[0].ivalue
                if index != 0:
                    src = get_float_reg_value(top, 0, current_float_stack_top)
                    store_temporary_register_to_float_reg(src, top, index, current_float_stack_top, last_float_reg_write[0])
            else:
                src = get_float_reg_value(top, 0, current_float_stack_top)
                tmp = get_new_temporary_register()
                dst = get_new_temporary_register()

                add_output_line(tmp + " = fptrunc double " + src + " to float")
                add_output_line(dst + " = bitcast float " + tmp + " to i32")
                store_temporary_register_to_reg(dst, "tmp0", last_reg_write[2])

            if word == "FSTP_FLOAT" or word == "FSTP_ST":
                increase_float_stack_top(top, 1, last_reg_write[1], current_float_stack_top)

            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        elif word in _BINARY_FLOAT:
            instr, ptype, rparam, rresult, pop = _BINARY_FLOAT[word]
            binary_float_instr(instr, ptype, rparam, rresult, pop, paramvals[0], last_reg_write[0], last_reg_write[1], current_float_stack_top, last_float_reg_write[0])
        elif word in _COMPARE_FLOAT:
            ptype, pop = _COMPARE_FLOAT[word]
            compare_float_instr(ptype, pop, paramvals[0], last_reg_write[0], last_reg_write[1], current_float_stack_top)
        elif word in ("FCOMPP_VOID", "FUCOMPP_VOID"):
            compare_float_instr(0, 2, Param("const", "1", 1), last_reg_write[0], last_reg_write[1], current_float_stack_top)
        elif word == "FMOD_VOID":
            binary_float_instr("frem", 0, False, True, True, Param("const", "1", 1), last_reg_write[0], last_reg_write[1], current_float_stack_top, last_float_reg_write[0])
        elif word == "FMODR_VOID":
            binary_float_instr("frem", 0, True, True, True, Param("const", "1", 1), last_reg_write[0], last_reg_write[1], current_float_stack_top, last_float_reg_write[0])
        elif word in _FLOAT_UNARY:
            top = get_parameter_read_value(Param("reg", "st_top"))
            src = get_float_reg_value(top, 0, current_float_stack_top)
            dst = get_new_temporary_register()

            instr = _FLOAT_UNARY[word]

            add_output_line(dst + " = call double @llvm." + instr + ".f64(double " + src + ")")

            store_temporary_register_to_float_reg(dst, top, 0, current_float_stack_top, last_float_reg_write[0])
            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        elif word == "FTAN_VOID":
            top1 = get_parameter_read_value(Param("reg", "st_top"))
            src = get_float_reg_value(top1, 0, current_float_stack_top)
            dst = get_new_temporary_register()

            add_output_line(dst + " = call double @llvm.tan.f64(double " + src + ")")

            store_temporary_register_to_float_reg(dst, top1, 0, current_float_stack_top, last_float_reg_write[0])

            tmp2 = get_new_temporary_register()
            top2 = get_new_temporary_register()

            add_output_line(tmp2 + " = add i32 " + top1 + ", 7")
            add_output_line(top2 + " = and i32 " + tmp2 + ", 7")

            store_temporary_register_to_float_reg("1.0", top2, 0, (current_float_stack_top + 7) & 7, last_float_reg_write[1])
            store_temporary_register_to_reg(top2, "st_top", last_reg_write[1])
            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        elif word in ("FPOW_VOID", "FPOWR_VOID"):
            top = get_parameter_read_value(Param("reg", "st_top"))
            src1 = get_float_reg_value(top, 0, current_float_stack_top)
            src2 = get_float_reg_value(top, 1, current_float_stack_top)
            dst = get_new_temporary_register()

            reverse_params = (word == "FPOWR_VOID")
            add_output_line(dst + " = call double @llvm.pow.f64(double " + (src2 if reverse_params else src1) + ", double " + (src1 if reverse_params else src2) + ")")

            store_temporary_register_to_float_reg(dst, top, 1, current_float_stack_top, last_float_reg_write[0])
            increase_float_stack_top(top, 1, last_reg_write[1], current_float_stack_top)
            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        elif word == "FYL2X_VOID":
            top = get_parameter_read_value(Param("reg", "st_top"))
            src1 = get_float_reg_value(top, 0, current_float_stack_top)
            tmp = get_new_temporary_register()

            add_output_line(tmp + " = call double @llvm.log2.f64(double " + src1 + ")")

            src2 = get_float_reg_value(top, 1, current_float_stack_top)
            dst = get_new_temporary_register()

            add_output_line(dst + " = fmul double " + src2 + ", " + tmp)

            store_temporary_register_to_float_reg(dst, top, 1, current_float_stack_top, last_float_reg_write[0])
            increase_float_stack_top(top, 1, last_reg_write[1], current_float_stack_top)
            store_temporary_register_to_reg("0", "st_sw_cond", last_reg_write[0])
        else:
            write_error2("Unhandled instruction: " + current_line.orig)
            return False

    if not G.no_tail_calls:
        add_output_line("ret void")
    return True


def write_output(output_filename):
    data = "".join(line + "\n" for line in G.output_lines).encode("latin-1")

    if output_filename == "":
        sys.stdout.flush()
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
    else:
        with open(output_filename, "wb") as fd:
            fd.write(data)


def write_usage():
    stderr_write("Usage: llasm [OPTIONS] INPUT_FILE")
    stderr_write("  -o OUTPUT_FILE  write output to a file instead of to stdout")
    stderr_write("  -I PATH         add directory to search path for includes")
    stderr_write("  -O              set optimization level 2")
    stderr_write("  -O2             set optimization level 2")
    stderr_write("  -O1             set optimization level 1")
    stderr_write("  -p              output preprocessed file")
    stderr_write("  -pic            generate position independent code")
    stderr_write("  -m64            set generated pointer size to 64 bits")
    stderr_write("  -ptrofs         use pointers with offset (requires -m64)")
    stderr_write("  -no-tail-calls  disable generating tail calls")
    stderr_write("  -inline-idiv    allow inlining integer division instructions")
    stderr_write("  -inline-float   allow inlining floating point instructions")
    stderr_write("  -inline-float2  allow inlining floating point intrinsics/functions")
    stderr_write("  -func-name A=B  rename function name A to B")


def _find_data_file(name):
    """The lookup of add_include_to_stack, for dinclude: returns the full path or None."""
    fullpath = None

    if not _is_absolute(name):
        path = _build_path(G.input_directory, name)
        if os.path.exists(path):
            fullpath = os.path.abspath(path)
        else:
            for d in G.include_directories:
                if _is_absolute(d):
                    path = _build_path(d, name)
                else:
                    path = _build_path(G.input_directory, d, name)

                if os.path.exists(path):
                    fullpath = os.path.abspath(path)
                    break

    if fullpath is None:
        if os.path.exists(name):
            fullpath = os.path.abspath(name)
    return fullpath


def _dd_extend(values, zero_fill_only_len=None):
    """Append bytes to the last data item, or start a new one (D: 'extend existing data' / 'add data')."""
    if G.dataseg_data and not G.dataseg_data[-1].isaddr:
        d = G.dataseg_data[-1]
        if not G.current_dataseg_isuninitialized:
            d.data += values
        d.datalen += len(values) if zero_fill_only_len is None else zero_fill_only_len
    else:
        d = DData()
        d.isaddr = False
        if not G.current_dataseg_isuninitialized:
            d.data = bytearray(values)
        d.datalen = len(values) if zero_fill_only_len is None else zero_fill_only_len
        d.addr = ""
        G.dataseg_data.append(d)


def _dd_extend_fill(value, count):
    if G.current_dataseg_isuninitialized:
        _dd_extend(b"", count)
    else:
        _dd_extend(bytes([value]) * count)


def main(args):
    reset_state()
    G.input_filename = ""
    G.output_filename = ""
    G.return_procedure = ""
    G.dispatcher_procedure = ""
    G.include_directories = []
    G.output_preprocessed_file = False
    G.global_optimization_level = 0
    G.position_independent_code = False
    G.old_bitcode = False
    G.no_tail_calls = False
    G.pointer_size_64 = False
    G.pointers_with_offset = False
    G.inline_idiv_instr = False
    G.inline_float_instr = False
    G.inline_float2_instr = False
    G.function_names = {}

    i = 1
    while i < len(args):
        a = args[i]
        if a == "-o":
            if i + 1 == len(args):
                stderr_write("Missing argument: " + args[i])
                write_usage()
                return 2
            G.output_filename = args[i + 1]
            i += 1
        elif a == "-I":
            if i + 1 == len(args):
                stderr_write("Missing argument: " + args[i])
                write_usage()
                return 2
            G.include_directories.append(args[i + 1])
            i += 1
        elif a in ("-O", "-O2"):
            G.global_optimization_level = 2
        elif a == "-O1":
            G.global_optimization_level = 1
        elif a == "-no-tail-calls":
            G.no_tail_calls = True
        elif a == "-p":
            G.output_preprocessed_file = True
        elif a == "-pic":
            G.position_independent_code = True
        elif a == "-m64":
            G.pointer_size_64 = True
        elif a == "-ptrofs":
            G.pointers_with_offset = True
        elif a == "-inline-idiv":
            G.inline_idiv_instr = True
        elif a == "-inline-float":
            G.inline_float_instr = True
        elif a == "-inline-float2":
            G.inline_float2_instr = True
        elif a == "-func-name":
            if i + 1 == len(args):
                stderr_write("Missing argument: " + args[i])
                write_usage()
                return 2

            fname = str_split_strip(args[i + 1], '=')
            if len(fname) != 2 or fname[0] == "" or fname[1] == "" or fname[0] in G.function_names:
                stderr_write("Bad argument: " + args[i] + " " + args[i + 1])
                write_usage()
                return 2

            G.function_names[fname[0]] = fname[1]
            i += 1
        else:
            if G.input_filename == "":
                G.input_filename = args[i]
            else:
                stderr_write("Bad argument: " + args[i])
                write_usage()
                return 1
        i += 1

    if G.pointers_with_offset and not G.pointer_size_64:
        stderr_write("Parameter -ptrofs requires also parameter -m64")
        write_usage()
        return 3

    if G.input_filename == "":
        stderr_write("Missing input filename")
        write_usage()
        return 3

    if not os.path.exists(G.input_filename):
        stderr_write("Input file not found: " + G.input_filename)
        write_usage()
        return 4

    initialize()

    G.input_directory = os.path.dirname(os.path.abspath(G.input_filename))

    G.file_input_level = 0
    e = FileInput()
    e.ismacro = False
    e.name = G.input_filename
    e.fullpath = os.path.abspath(G.input_filename)
    e.linenum = 0
    e.lines = _read_file_lines(G.input_filename)
    G.file_input_stack = [e]

    G.end_of_input = False
    G.input_reading_proc = False
    G.input_reading_dataseg = False
    G.local_proc_names = []

    G.output_lines = []

    while True:
        read_next_line()
        if G.end_of_input:
            break
        cl = G.current_line

        # preprocessing
        if cl.word == "include":
            if G.output_preprocessed_file:
                add_output_line(";" + cl.orig)

            if cl.params == "":
                write_error("Missing include filename")
                return 5

            if not add_include_to_stack(cl.params):
                return 6

            continue

        if cl.word == "macro":
            if G.output_preprocessed_file:
                add_output_line(";" + cl.orig)

            if cl.param1 == "":
                write_error("Missing macro name")
                return 7

            if cl.param1 in G.macro_list:
                write_error("Macro already defined: " + cl.param1)
                return 8

            if cl.param1 in G.keywords_list or (cl.param1 in G.instructions_list and not G.instructions_list[cl.param1].inlined_instr):
                write_error("Macro name equals keyword/instruction: " + cl.param1)
                return 9

            newmacro = MacroDef()

            newmacro.name = cl.param1
            newmacro.params = get_current_params2()
            newmacro.lines = []

            # sorted by parameter name length, the longest first
            newmacro.index = sorted(range(len(newmacro.params)), key=lambda k: -len(newmacro.params[k]))

            while True:
                read_next_line()
                if G.end_of_input:
                    stderr_write("Missing end of macro: " + newmacro.name)
                    return 10

                cl = G.current_line

                if cl.word == "endm":
                    if cl.params != "":
                        write_error("Additional data after macro end: " + newmacro.name)
                        return 11

                    break

                newmacro.lines.append(cl.orig)

            if (newmacro.name not in G.instructions_list) or (len(newmacro.params) != len(G.instructions_list[newmacro.name].params)):
                G.macro_list[newmacro.name] = newmacro

            continue

        if cl.word in G.macro_list:
            if G.output_preprocessed_file:
                add_output_line(";" + cl.orig)

            if not add_macro_to_stack(cl.word, get_current_params()):
                return 12

            continue

        if G.output_preprocessed_file:
            add_output_line(cl.orig)
            continue

        if cl.word == "":
            continue

        # normal processing
        if cl.word == "define":
            if G.input_reading_proc or G.input_reading_dataseg:
                write_error("Define inside proc/dataseg")
                return 13

            if cl.param1 == "":
                write_error("Define name missing")
                return 14

            if cl.param1 in G.define_list:
                write_error("Define already defined: " + cl.param1)
                return 15

            if cl.param1 in G.func_list or cl.param1 in G.registers_list or cl.param1 in G.dlabel_list or cl.param1 in G.dataseg_list:
                write_error("Define equals func/register/dlabel/dataseg: " + cl.param1)
                return 16

            if cl.param1 in G.proc_list or cl.param1 in G.extern_list:
                write_error("Define defined after proc/extern: " + cl.param1)
                return 17

            if cl.param1 == cl.params:
                write_error("Define value missing: " + cl.param1)
                return 18

            G.define_list[cl.param1] = strip(cl.params[len(cl.param1) + 1:])

            continue

        if cl.word == "extern":
            if G.input_reading_proc or G.input_reading_dataseg:
                write_error("Extern inside proc/dataseg")
                return 19

            if cl.param1 == "":
                write_error("Extern name missing")
                return 20

            if cl.param1 in G.define_list:
                extern_name = G.define_list[cl.param1]

                if extern_name.find(' ') != -1:
                    write_error("Extern name contains spaces: " + extern_name + " (" + cl.param1 + ")")
                    return 21
            else:
                extern_name = cl.param1

            if extern_name in G.extern_list:
                write_error("Extern already defined: " + extern_name + " (" + cl.param1 + ")")
                return 22

            if extern_name in G.func_list or extern_name in G.proc_list or extern_name in G.dlabel_list or extern_name in G.dataseg_list:
                write_error("Extern equals func/proc/dlabel/dataseg: " + extern_name + " (" + cl.param1 + ")")
                return 23

            if cl.param1 != cl.params:
                write_error("Additional data after extern: " + cl.params)
                return 24

            G.extern_list[extern_name] = True

            continue

        if cl.word in ("func", "funcp", "funcv"):
            if G.input_reading_proc or G.input_reading_dataseg:
                write_error("Func inside proc/dataseg")
                return 25

            if cl.param1 == "":
                write_error("Func name missing")
                return 26

            if cl.param1 in G.func_list:
                write_error("Func already defined: " + cl.param1)
                return 27

            if cl.param1 in G.define_list or cl.param1 in G.extern_list or cl.param1 in G.proc_list or cl.param1 in G.dlabel_list or cl.param1 in G.dataseg_list:
                write_error("Func equals define/extern/proc/dlabel/dataseg: " + cl.param1)
                return 28

            newfunc = FuncDef()

            newfunc.real_name = G.function_names[cl.param1] if cl.param1 in G.function_names else cl.param1
            newfunc.has_return_value = (cl.word == "func" or cl.word == "funcp")
            newfunc.return_value_is_pointer = (cl.word == "funcp")
            newfunc.params = get_functions_params(get_current_params2())

            G.func_list[cl.param1] = newfunc

            continue

        if cl.word == "proc":
            if G.input_reading_proc or G.input_reading_dataseg:
                write_error("Proc inside proc/dataseg")
                return 29

            if cl.param1 == "":
                write_error("Proc name missing")
                return 30

            if cl.param1 in G.define_list:
                alias_name = cl.param1
                proc_name = G.define_list[cl.param1]

                if proc_name.find(' ') != -1:
                    write_error("Proc name contains spaces: " + proc_name + " (" + alias_name + ")")
                    return 31
            else:
                proc_name = cl.param1
                alias_name = ""

            if proc_name in G.proc_list:
                write_error("Proc already defined: " + proc_name + " (" + alias_name + ")")
                return 32

            if proc_name in G.extern_list or proc_name in G.func_list or proc_name in G.dlabel_list or proc_name in G.dataseg_list:
                write_error("Proc equals extern/func/dlabel/dataseg: " + proc_name + " (" + alias_name + ")")
                return 33

            newproc = ProcDef()

            newproc.name = proc_name
            newproc.alias_name = alias_name

            newproc.isexternal = False
            newproc.ispublic = False
            newproc.isglobal = False
            newproc.global_name = ""
            newproc.lines = []

            if cl.param1 == cl.params:
                G.input_reading_proc = True
            else:
                st = strip(cl.params[len(cl.param1) + 1:])

                if st == "external":
                    newproc.isexternal = True
                elif st == "public":
                    newproc.ispublic = True
                    G.input_reading_proc = True
                else:
                    position = st.find(' ')

                    if position >= 0 and st[0:position] == "global":
                        newproc.isglobal = True
                        G.input_reading_proc = True

                        st = strip(st[position + 1:])

                        position = st.find(' ')
                        if position >= 0:
                            write_error("Global proc name contains spaces: " + st)
                            return 34
                        else:
                            newproc.global_name = st
                    else:
                        write_error("Unknown aditional data after proc: " + st)
                        return 35

            G.proc_list[proc_name] = newproc

            G.current_proc = newproc.name
            G.current_proc_lines = 0
            G.proc_instructions = []

            if not newproc.isexternal:
                G.local_proc_names.append(newproc.name)

            continue

        if cl.word == "endp":
            if not G.input_reading_proc:
                write_error("Endp outside proc")
                return 36

            if cl.params != "":
                write_error("Additional data after endp: " + cl.params)
                return 37

            G.input_reading_proc = False

            curproc = G.proc_list[G.current_proc]
            curproc.lines = list(G.proc_instructions[0:G.current_proc_lines])

            continue

        if cl.word in G.instructions_list:
            if not G.input_reading_proc:
                write_error("Instruction outside proc")
                return 38

            if not check_instruction_pass1(cl):
                return 39

            del G.proc_instructions[G.current_proc_lines:]
            G.proc_instructions.append(cl)
            G.current_proc_lines += 1

            continue

        if cl.word == "datasegment":
            if G.input_reading_proc or G.input_reading_dataseg:
                write_error("Datasegment inside proc/dataseg")
                return 40

            if cl.param1 == "":
                write_error("Datasegment name missing")
                return 41

            dataseg_name = cl.param1.replace(".", "_")

            if dataseg_name != cl.param1:
                stderr_write("Replacing datasegment name " + cl.param1 + " with " + dataseg_name)

            if dataseg_name in G.dataseg_list:
                write_error("Datasegment already defined: " + dataseg_name)
                return 42

            if dataseg_name in G.proc_list or dataseg_name in G.extern_list or dataseg_name in G.func_list or dataseg_name in G.dlabel_list:
                write_error("Datasegment equals proc/extern/func/dlabel: " + dataseg_name)
                return 43

            newdataseg = DataSeg()

            newdataseg.name = dataseg_name
            newdataseg.isconstant = False
            newdataseg.isuninitialized = False
            newdataseg.addr_in_ctor = False

            G.current_dataseg = newdataseg.name
            G.current_dataseg_offset = 0
            G.current_dataseg_numlabels = 0
            G.dataseg_labels = []
            G.current_dataseg_numdata = 0
            G.dataseg_data = []
            G.current_dataseg_isuninitialized = False

            G.input_reading_dataseg = True

            if cl.param1 != cl.params:
                st = strip(cl.params[len(cl.param1) + 1:])

                if st == "constant":
                    newdataseg.isconstant = True
                elif st == "uninitialized":
                    newdataseg.isuninitialized = True
                    G.current_dataseg_isuninitialized = True
                else:
                    write_error("Unknown aditional data after dataseg: " + st)
                    return 44

            G.dataseg_list[dataseg_name] = newdataseg

            continue

        if cl.word == "dlabel":
            if not G.input_reading_dataseg:
                write_error("Dlabel outside dataseg")
                return 45

            if cl.param1 == "":
                write_error("Dlabel name missing")
                return 46

            if cl.param1 in G.dlabel_list:
                write_error("Dlabel already defined: " + cl.param1)
                return 47

            if cl.param1 in G.proc_list or cl.param1 in G.extern_list or cl.param1 in G.func_list or cl.param1 in G.dataseg_list:
                write_error("Dlabel equals proc/extern/func/dataseg: " + cl.param1)
                return 48

            newlabel = DLabel()

            newlabel.name = cl.param1
            newlabel.dataseg_name = G.current_dataseg
            newlabel.offset = G.current_dataseg_offset
            newlabel.isglobal = False

            if cl.param1 != cl.params:
                st = strip(cl.params[len(cl.param1) + 1:])

                if st == "global":
                    newlabel.isglobal = True
                else:
                    write_error("Unknown aditional data after dlabel: " + st)
                    return 49

            G.dataseg_labels.append(newlabel)
            G.current_dataseg_numlabels += 1

            G.dlabel_list[cl.param1] = newlabel

            continue

        if cl.word == "dalign":
            if not G.input_reading_dataseg:
                write_error("Dalign outside dataseg")
                return 50

            if cl.param1 == "":
                write_error("Dalign parameter missing")
                return 51

            if not is_number(cl.param1):
                write_error("Dalign parameter is not a number: " + cl.param1)
                return 52

            if cl.param1 != cl.params:
                write_error("Unknown aditional data after dalign: " + cl.params)
                return 53

            alignnum = _s32(get_number_value(cl.param1))

            if alignnum != 1 and alignnum != 2 and alignnum != 4:
                write_error("Dalign has wrong alignment: " + cl.param1)
                return 54

            if alignnum > 1:
                alignment = (G.current_dataseg_offset if G.current_dataseg_offset >= 0 else -((-G.current_dataseg_offset) % alignnum)) % alignnum
                if alignment > 0:
                    numbytes = alignnum - alignment

                    _dd_extend_fill(0, numbytes)

                    G.current_dataseg_offset = _s32(G.current_dataseg_offset + numbytes)

            G.current_dataseg_numdata = len(G.dataseg_data)

            continue

        if cl.word == "db":
            if not G.input_reading_dataseg:
                write_error("Db outside dataseg")
                return 55

            if G.current_dataseg_isuninitialized:
                write_error("Db inside uninitialized dataseg")
                return 56

            if cl.param1 == "":
                write_error("Db parameter missing")
                return 57

            if not is_number(cl.param1):
                write_error("Db parameter is not a number: " + cl.param1)
                return 58

            dbdup = 1

            if cl.param1 != cl.params:
                st = strip(cl.params[len(cl.param1) + 1:])

                position = st.find(' ')

                if position >= 0 and st[0:position] == "dup":
                    st = strip(st[position + 1:])

                    if not is_number(st):
                        write_error("Db dup parameter is not a number: " + st)
                        return 59

                    dbdup = get_number_value(st)
                    if dbdup < -0x80000000 or dbdup > 0x7FFFFFFF:
                        raise OverflowError("Overflow in integer conversion")

                    if dbdup < 1:
                        write_error("Db dup parameter is less than 1: " + st)
                        return 60
                else:
                    write_error("Unknown aditional data after db: " + st)
                    return 61

            value = get_number_value(cl.param1)
            if value < 0 or value > 255:
                raise OverflowError("Value does not fit in ubyte")

            _dd_extend_fill(value, dbdup)

            G.current_dataseg_offset = _s32(G.current_dataseg_offset + dbdup)
            G.current_dataseg_numdata = len(G.dataseg_data)

            continue

        if cl.word == "dinclude":
            if not G.input_reading_dataseg:
                write_error("Dinclude outside dataseg")
                return 62

            if G.current_dataseg_isuninitialized:
                write_error("Dinclude inside uninitialized dataseg")
                return 63

            if cl.params == "":
                write_error("Dinclude filename missing")
                return 64

            fullpath = _find_data_file(cl.params)
            if fullpath is None:
                write_error("Dinclude not found: " + cl.params)
                return 65

            with open(fullpath, "rb") as fd:
                values = fd.read()

            if len(values) > 0:
                _dd_extend(values)

                G.current_dataseg_offset = _s32(G.current_dataseg_offset + len(values))
                G.current_dataseg_numdata = len(G.dataseg_data)

            continue

        if cl.word == "daddr":
            if not G.input_reading_dataseg:
                write_error("Daddr outside dataseg")
                return 66

            if G.current_dataseg_isuninitialized:
                write_error("Daddr inside uninitialized dataseg")
                return 67

            if cl.param1 == "":
                write_error("Daddr parameter missing")
                return 68

            if cl.param1 != cl.params:
                write_error("Unknown aditional data after daddr: " + cl.params)
                return 69

            if cl.param1 in G.define_list:
                param = G.define_list[cl.param1]
            else:
                param = cl.param1

            addr_offset = 0
            plus = param.find('+')
            minus = param.find('-')
            idx = -1
            neg = False
            if plus > 0:
                idx = plus
            elif minus > 0:
                idx = minus
                neg = True
            if idx >= 0:
                offstr = param[idx + 1:]
                addr_offset = _to_int(offstr)
                if neg:
                    addr_offset = _s32(-addr_offset)
                param = param[0:idx]

            d = DData()
            d.isaddr = True
            d.data = bytearray()
            d.datalen = 0
            d.addr = param
            d.addr_offset = addr_offset
            G.dataseg_data.append(d)
            G.current_dataseg_numdata = len(G.dataseg_data)

            G.current_dataseg_offset = _s32(G.current_dataseg_offset + 4)

            continue

        if cl.word == "dskip":
            if not G.input_reading_dataseg:
                write_error("Dskip outside dataseg")
                return 70

            if not G.current_dataseg_isuninitialized:
                write_error("Dskip inside initialized dataseg")
                return 71

            if cl.param1 == "":
                write_error("Dskip parameter missing")
                return 72

            if not is_number(cl.param1):
                write_error("Dskip parameter is not a number: " + cl.param1)
                return 73

            if cl.param1 != cl.params:
                write_error("Unknown aditional data after dskip: " + cl.params)
                return 74

            value = get_number_value(cl.param1)
            if value < -0x80000000 or value > 0x7FFFFFFF:
                raise OverflowError("Overflow in integer conversion")

            if value < 1:
                write_error("Dskip parameter is less than 1: " + cl.param1)
                return 75

            _dd_extend_fill(0, value)

            G.current_dataseg_offset = _s32(G.current_dataseg_offset + value)
            G.current_dataseg_numdata = len(G.dataseg_data)

            continue

        if cl.word == "endd":
            if not G.input_reading_dataseg:
                write_error("Endd outside dataseg")
                return 76

            if cl.param1 != cl.params:
                write_error("Unknown aditional data after endd: " + cl.params)
                return 77

            if G.current_dataseg_offset == 0:
                write_error("Empty dataseg: " + G.current_dataseg)
                return 78

            newdataseg = G.dataseg_list[G.current_dataseg]

            newdataseg.labels = list(G.dataseg_labels[0:G.current_dataseg_numlabels])
            newdataseg.data = list(G.dataseg_data[0:G.current_dataseg_numdata])

            G.input_reading_dataseg = False

            continue

        write_error("Unknown word: " + cl.word)
        return 79

    if G.input_reading_proc:
        stderr_write("Missing end of proc")
        return 80

    if G.input_reading_dataseg:
        stderr_write("Missing end of datasegment")
        return 81

    # llasm.d walks its associative array here, in druntime's hash order, so
    # with several bad daddr in different datasegments it may name another one
    # than this (the status is the same); every other walk is by sorted name.
    for dataseg in G.dataseg_list.values():
        for data in dataseg.data:
            if data.isaddr:
                if data.addr not in G.proc_list and data.addr not in G.dlabel_list:
                    stderr_write("Daddr not a proc nor a dlabel: " + data.addr)
                    return 82

    if G.output_preprocessed_file:
        write_output(G.output_filename)
        return 0

    # second pass

    if G.pointer_size_64:
        add_output_line("target datalayout = \"e-p:64:64-f64:64:64-n32:64\"")
    else:
        add_output_line("target datalayout = \"e-p:32:32-f64:32:64-n32\"")
    add_output_line("")
    add_output_line("%_cpu = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [8 x double], i64 }")

    add_output_line("")
    add_output_line("; data segments")
    for dataseg_name in sorted(G.dataseg_list.keys()):
        dataseg = G.dataseg_list[dataseg_name]

        dataseg_type = ""
        dataseg_values = ""
        for i in range(len(dataseg.data)):
            dd = dataseg.data[i]
            if i > 0:
                dataseg_type += ", "
                dataseg_values += ", "
            if dd.isaddr:
                dataseg_type += "i32"
                dataseg_values += "i32 "

                if G.pointers_with_offset or (G.position_independent_code and G.pointer_size_64):
                    dataseg_values += "0"
                    dataseg.addr_in_ctor = True
                    G.create_ctor_function = True
                elif dd.addr in G.proc_list:
                    dataseg_values += "ptrtoint (" + ("i8*" if G.no_tail_calls else "void") + "(%_cpu*)* " + _llref(dd.addr) + " to i32)"
                else:
                    if G.position_independent_code:
                        addrlabel = G.dlabel_list[dd.addr]

                        dataseg_values += "ptrtoint (i8* getelementptr (" + get_load_type("i8") + " bitcast (%_" + addrlabel.dataseg_name + "* " + _llref(addrlabel.dataseg_name) + " to i8*), i32 " + str(_s32(addrlabel.offset + dd.addr_offset)) + ") to i32)"
                    else:
                        if dd.addr_offset == 0:
                            dataseg_values += "ptrtoint (i8* " + _llref(dd.addr) + " to i32)"
                        else:
                            dataseg_values += "ptrtoint (i8* getelementptr (" + get_load_type("i8") + " " + _llref(dd.addr) + ", i32 " + str(dd.addr_offset) + ") to i32)"
            else:
                dataseg_type += "[" + str(dd.datalen) + " x i8]"
                dataseg_values += "[" + str(dd.datalen) + " x i8]"
                if dataseg.isuninitialized:
                    dataseg_values += " undef"
                else:
                    dataseg_values += " c\""
                    dataseg_values += "".join("\\%02X" % b for b in dd.data)
                    dataseg_values += "\""

        add_output_line("%_" + dataseg_name + " = type <{ " + dataseg_type + " }>")
        add_output_line(_llref(dataseg_name) + " = private " + ("constant" if (dataseg.isconstant and not dataseg.addr_in_ctor) else "global") + " %_" + dataseg_name + " <{ " + dataseg_values + " }>, align 4")

        for label in dataseg.labels:
            if (not G.position_independent_code) or label.isglobal:
                add_output_line(_llref(label.name) + " = " + ("protected" if label.isglobal else "private") + " alias " + get_load_type("i8") + " getelementptr (" + get_load_type("i8") + " bitcast (%_" + dataseg_name + "* " + _llref(dataseg_name) + " to i8*), i32 " + str(label.offset) + ")")

    add_output_line("")
    add_output_line("; external variables")
    for extern_name in sorted(G.extern_list.keys()):
        add_output_line(_llref(extern_name) + " = external global i8")

    add_output_line("")
    add_output_line("; external functions")
    for func_name in sorted(G.func_list.keys()):
        func = G.func_list[func_name]

        func_args = ""
        for i in range(len(func.params)):
            if func.params[i].type == 2:
                datatype = "%_cpu*"
            elif func.params[i].type == 1:
                datatype = "i8*"
            else:
                datatype = "i32"

            if i > 0:
                func_args += ","
            func_args += datatype

        add_output_line("declare ccc " + (("i8*" if func.return_value_is_pointer else "i32") if func.has_return_value else "void") + " " + _llref(func.real_name) + "(" + func_args + ") nounwind")

    add_output_line("")
    add_output_line("; external procedures")
    for proc_name in sorted(G.proc_list.keys()):
        proc = G.proc_list[proc_name]

        if not proc.isexternal:
            continue

        add_output_line("declare hidden fastcc " + ("i8*" if G.no_tail_calls else "void") + " " + _llref(proc_name) + "(%_cpu*) nounwind")

    add_output_line("")
    add_output_line("; intrinsics")
    if G.used_ctlz_intrinsics:
        add_output_line("declare i32 @llvm.ctlz.i32(i32, i1)")
    if G.used_bswap_intrinsics:
        add_output_line("declare i32 @llvm.bswap.i32(i32)")
    if G.used_sqrt_intrinsics:
        add_output_line("declare double @llvm.sqrt.f64(double)")
    if G.used_sin_intrinsics:
        add_output_line("declare double @llvm.sin.f64(double)")
    if G.used_cos_intrinsics:
        add_output_line("declare double @llvm.cos.f64(double)")
    if G.used_tan_intrinsics:
        add_output_line("declare double @llvm.tan.f64(double)")
    if G.used_log_intrinsics:
        add_output_line("declare double @llvm.log.f64(double)")
    if G.used_log10_intrinsics:
        add_output_line("declare double @llvm.log10.f64(double)")
    if G.used_fabs_intrinsics:
        add_output_line("declare double @llvm.fabs.f64(double)")
    if G.used_round_intrinsics:
        add_output_line("declare double @llvm.round.f64(double)")
    if G.used_pow_intrinsics:
        add_output_line("declare double @llvm.pow.f64(double, double)")
    if G.used_log2_intrinsics:
        add_output_line("declare double @llvm.log2.f64(double)")

    add_output_line("")
    add_output_line("; procedures")
    for proc_name in G.local_proc_names:
        proc = G.proc_list[proc_name]

        add_output_line("define " + ("protected" if proc.isglobal else ("hidden" if proc.ispublic else "private")) + " fastcc " + ("i8*" if G.no_tail_calls else "void") + " " + _llref(proc_name) + "(%_cpu* %cpu) nounwind {")

        if not process_proc_body(proc_name):
            return 79

        add_output_line("}")

        if proc.isglobal:
            if G.return_procedure == "":
                G.return_procedure = "__return_procedure"
                if G.no_tail_calls:
                    add_output_line("define private fastcc i8* " + _llref(G.return_procedure) + "(%_cpu* %cpu) nounwind {")
                    add_output_line("ret i8* null")
                else:
                    add_output_line("define private fastcc void " + _llref(G.return_procedure) + "(%_cpu* %cpu) nounwind {")
                    add_output_line("ret void")
                add_output_line("}")

            if G.no_tail_calls and G.dispatcher_procedure == "":
                G.dispatcher_procedure = "__dispatcher_procedure"
                add_output_line("define private fastcc void " + _llref(G.dispatcher_procedure) + "(%_cpu* %cpu, i8*(%_cpu*)* %proc) nounwind {")

                aligntproc = ", align " + ("8" if G.pointer_size_64 else "4")
                add_output_line("%tproc = alloca i8*(%_cpu*)*" + aligntproc)

                add_output_line("%1 = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + G.register_numbers_str["esp"])
                add_output_line("%2 = load " + get_load_type("i32") + " %1, align 4")
                add_output_line("%3 = sub i32 %2, 4")
                add_output_line("store i32 %3, i32* %1, align 4")

                G.procedure_optimization_level = 2
                G.temporary_register_index = 3
                for k in range(NUM_REGS64):
                    G.register64_state[k] = EMPTY

                r4 = get_int32_to_ptr("%3", "i32")
                r5 = get_addr_to_int32(_llref(G.return_procedure), ("i8*" if G.no_tail_calls else "void") + "(%_cpu*)")
                add_output_line("store i32 " + r5 + ", i32* " + r4 + ", align 4")

                add_output_line("store i8*(%_cpu*)* %proc, i8*(%_cpu*)** %tproc" + aligntproc)
                add_output_line("br label %l_1")

                add_output_line("l_1:")
                r6 = get_new_temporary_register()
                r7 = get_new_temporary_register()
                r8 = get_new_temporary_register()
                r9 = get_new_temporary_register()
                add_output_line(r6 + " = load " + get_load_type("i8*(%_cpu*)*") + " %tproc" + aligntproc)
                add_output_line(r7 + " = call fastcc i8* " + r6 + "(%_cpu* %cpu) nounwind")
                add_output_line(r8 + " = bitcast i8* " + r7 + " to i8*(%_cpu*)*")
                add_output_line("store i8*(%_cpu*)* " + r8 + ", i8*(%_cpu*)** %tproc" + aligntproc)
                add_output_line(r9 + " = icmp eq i8* " + r7 + ", null")
                add_output_line("br i1 " + r9 + ", label %l_2, label %l_1")
                add_output_line("l_2:")

                add_output_line("ret void")
                add_output_line("}")

            add_output_line("define protected ccc void " + _llref(proc.global_name) + "(%_cpu* %cpu) nounwind {")

            if G.no_tail_calls:
                add_output_line("tail call fastcc void " + _llref(G.dispatcher_procedure) + "(%_cpu* %cpu, i8*(%_cpu*)* " + _llref(proc_name) + ") nounwind")
            else:
                add_output_line("%1 = getelementptr " + get_load_type("%_cpu") + " %cpu, i32 0, i32 " + G.register_numbers_str["esp"])
                add_output_line("%2 = load " + get_load_type("i32") + " %1, align 4")
                add_output_line("%3 = sub i32 %2, 4")
                add_output_line("store i32 %3, i32* %1, align 4")

                G.procedure_optimization_level = 2
                G.temporary_register_index = 3
                for k in range(NUM_REGS64):
                    G.register64_state[k] = EMPTY

                r4 = get_int32_to_ptr("%3", "i32")
                r5 = get_addr_to_int32(_llref(G.return_procedure), ("i8*" if G.no_tail_calls else "void") + "(%_cpu*)")
                add_output_line("store i32 " + r5 + ", i32* " + r4 + ", align 4")

                add_output_line("call fastcc void " + _llref(proc_name) + "(%_cpu* %cpu) nounwind")

            add_output_line("ret void")
            add_output_line("}")

    if G.pointers_with_offset:
        add_output_line("")
        add_output_line("; pointer initialization")
        add_output_line("@ptr_initialize_pointers = common global void(i64)* null, align 8")

    if G.create_ctor_function:
        add_output_line("")
        add_output_line("; ctor")
        add_output_line("%_ctors = type { i32, void ()*, i8* }")
        add_output_line("@llvm.global_ctors = appending global [1 x %_ctors] [%_ctors { i32 65535, void ()* @__ctor, i8* null }]")
        add_output_line("")
        if G.pointers_with_offset:
            add_output_line("@ptr_prev_init_pointers = private global void(i64)* null, align 8")
            add_output_line("")
            add_output_line("define private ccc void @initialize_pointers(i64 %ptrofs) nounwind {")
            add_output_line("%1 = trunc i64 %ptrofs to i32")

            G.procedure_optimization_level = 2
            G.temporary_register_index = 1
            for k in range(NUM_REGS64):
                G.register64_state[k] = EMPTY
        else:
            add_output_line("define private void @__ctor() nounwind {")

        for dataseg_name in sorted(G.dataseg_list.keys()):
            dataseg = G.dataseg_list[dataseg_name]

            if not dataseg.addr_in_ctor:
                continue

            dataseg_offset = 0
            for i in range(len(dataseg.data)):
                dd = dataseg.data[i]
                if not dd.isaddr:
                    dataseg_offset = _s32(dataseg_offset + dd.datalen)
                    continue

                if dd.addr in G.proc_list:
                    store_value = "ptrtoint (" + ("i8*" if G.no_tail_calls else "void") + "(%_cpu*)* " + _llref(dd.addr) + " to i32)"
                else:
                    if G.position_independent_code:
                        addrlabel = G.dlabel_list[dd.addr]

                        store_value = "ptrtoint (i8* getelementptr (" + get_load_type("i8") + " bitcast (%_" + addrlabel.dataseg_name + "* " + _llref(addrlabel.dataseg_name) + " to i8*), i32 " + str(_s32(addrlabel.offset + dd.addr_offset)) + ") to i32)"
                    else:
                        if dd.addr_offset == 0:
                            store_value = "ptrtoint (i8* " + _llref(dd.addr) + " to i32)"
                        else:
                            store_value = "ptrtoint (i8* getelementptr (" + get_load_type("i8") + " " + _llref(dd.addr) + ", i32 " + str(dd.addr_offset) + ") to i32)"

                ptr_value = "bitcast (i8* getelementptr (" + get_load_type("i8") + " bitcast (%_" + dataseg_name + "* " + _llref(dataseg_name) + " to i8*), i32 " + str(dataseg_offset) + ") to i32*)"

                align_value = 4 if (dataseg_offset % 4 == 0) else (2 if (dataseg_offset % 2 == 0) else 1)

                if G.pointers_with_offset:
                    r1 = get_new_temporary_register()
                    add_output_line(r1 + " = sub i32 " + store_value + ", %1")
                    add_output_line("store i32 " + r1 + ", i32* " + ptr_value + ", align " + str(align_value))
                else:
                    add_output_line("store i32 " + store_value + ", i32* " + ptr_value + ", align " + str(align_value))

                dataseg_offset = _s32(dataseg_offset + 4)

        if G.pointers_with_offset:
            r2 = get_new_temporary_register()
            r3 = get_new_temporary_register()
            add_output_line(r2 + " = load void(i64)*, void(i64)** @ptr_prev_init_pointers, align 8")
            add_output_line(r3 + " = icmp eq void(i64)* " + r2 + ", null")
            add_output_line("br i1 " + r3 + ", label %l_2, label %l_1")
            add_output_line("l_1:")
            add_output_line("tail call ccc void " + r2 + "(i64 %ptrofs) nounwind")
            add_output_line("ret void")
            add_output_line("l_2:")
            add_output_line("ret void")
            add_output_line("}")
            add_output_line("")
            add_output_line("define private void @__ctor() nounwind {")
            add_output_line("%1 = load void(i64)*, void(i64)** @ptr_initialize_pointers, align 8")
            add_output_line("store void(i64)* %1, void(i64)** @ptr_prev_init_pointers, align 8")
            add_output_line("store void(i64)* @initialize_pointers, void(i64)** @ptr_initialize_pointers, align 8")
        add_output_line("ret void")
        add_output_line("}")

    write_output(G.output_filename)
    return 0


def _to_int(s):
    v = _to_long(s)
    if v < -0x80000000 or v > 0x7FFFFFFF:
        raise OverflowError("Overflow in integer conversion")
    return v


if __name__ == "__main__":
    sys.exit(main(sys.argv))
