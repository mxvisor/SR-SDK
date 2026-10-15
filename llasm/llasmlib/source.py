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

"""Reading the input: lines, includes, macros, instruction tables."""

import os

from .state import FileInput, FuncParam, G, InstrDef, Line, stderr_write
from .strutil import lstrip, str_split_strip, strip
from .tables import NUM_REGS, NUM_REGS64, NUM_REGS_VALID, REGS64_START_NUM, _INTRINSIC_FLAGS, instructions_base_list, instructions_float2_list, instructions_float_list, instructions_idiv_list, keywords_base_list, registers64_base_list, registers_base_list


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
