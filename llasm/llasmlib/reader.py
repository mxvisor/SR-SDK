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

"""Reading the input: options-independent first pass over the llasm file."""

import os

from .emit import add_output_line
from .expr import _s32, _to_int, get_number_value, is_number
from .source import _build_path, _is_absolute, _read_file_lines, add_include_to_stack, add_macro_to_stack, check_instruction_pass1, get_current_params, get_current_params2, get_functions_params, initialize, read_next_line, write_error
from .state import DData, DLabel, DataSeg, FileInput, FuncDef, G, MacroDef, ProcDef, stderr_write
from .strutil import strip


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


def _read_define(cl):
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

    return None
    return None



def _read_extern(cl):
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

    return None
    return None



def _read_func(cl):
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

    return None
    return None



def _read_proc(cl):
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

    return None
    return None



def _read_endp(cl):
    if not G.input_reading_proc:
        write_error("Endp outside proc")
        return 36

    if cl.params != "":
        write_error("Additional data after endp: " + cl.params)
        return 37

    G.input_reading_proc = False

    curproc = G.proc_list[G.current_proc]
    curproc.lines = list(G.proc_instructions[0:G.current_proc_lines])

    return None
    return None



def _read_instruction(cl):
    if not G.input_reading_proc:
        write_error("Instruction outside proc")
        return 38

    if not check_instruction_pass1(cl):
        return 39

    del G.proc_instructions[G.current_proc_lines:]
    G.proc_instructions.append(cl)
    G.current_proc_lines += 1

    return None
    return None



def _read_datasegment(cl):
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

    return None
    return None



def _read_dlabel(cl):
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

    # a label of a disassembled program is named after its address
    if cl.param1.startswith("loc_"):
        try:
            newlabel.ivalue = int(cl.param1[len("loc_"):], 16)
        except ValueError:
            newlabel.ivalue = 0

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

    return None
    return None



def _read_dalign(cl):
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

    return None
    return None



def _read_db(cl):
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

    return None
    return None



def _read_dinclude(cl):
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

    return None
    return None



def _read_daddr(cl):
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

    return None
    return None



def _read_dskip(cl):
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

    return None
    return None



def _read_endd(cl):
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

    return None
    return None



_READ_BEFORE_INSTRUCTION = {
    'define': _read_define,
    'extern': _read_extern,
    'func': _read_func,
    'funcp': _read_func,
    'funcv': _read_func,
    'proc': _read_proc,
    'endp': _read_endp,
}



_READ_AFTER_INSTRUCTION = {
    'datasegment': _read_datasegment,
    'dlabel': _read_dlabel,
    'dalign': _read_dalign,
    'db': _read_db,
    'dinclude': _read_dinclude,
    'daddr': _read_daddr,
    'dskip': _read_dskip,
    'endd': _read_endd,
}



def read_input():
    """Open the input file and read it all: includes, macros, definitions, procedures, data.
    Returns an exit status, or None when the input was read without errors."""

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
        if cl.word in _READ_BEFORE_INSTRUCTION:
            handler = _READ_BEFORE_INSTRUCTION[cl.word]
        elif cl.word in G.instructions_list:
            handler = _read_instruction
        elif cl.word in _READ_AFTER_INSTRUCTION:
            handler = _READ_AFTER_INSTRUCTION[cl.word]
        else:
            write_error("Unknown word: " + cl.word)
            return 79

        result = handler(cl)
        if result is not None:
            return result


    if G.input_reading_proc:
        stderr_write("Missing end of proc")
        return 80

    if G.input_reading_dataseg:
        stderr_write("Missing end of datasegment")
        return 81

    # llasm.d walks its associative array here, in druntime's hash order, so
    # with several bad daddr in different datasegments it may name another one
    # than this (the status is the same); every other walk is by sorted name.
    labels = {}
    for name in list(G.proc_list) + list(G.dlabel_list):
        if name.startswith("loc_"):
            try:
                labels[name] = int(name[4:], 16)
            except ValueError:
                pass
    ordered = sorted((address, name) for name, address in labels.items())
    for dataseg in G.dataseg_list.values():
        for data in dataseg.data:
            if not data.isaddr:
                continue
            if data.addr in G.proc_list or data.addr in G.dlabel_list:
                continue
            # A pointer may land between two labels, e.g. on a jump table
            # inside a translated procedure: express it from the nearest
            # label before it instead of rejecting it.
            address = None
            if data.addr.startswith("loc_"):
                try:
                    address = int(data.addr[4:], 16)
                except ValueError:
                    pass
            base = None
            if address is not None:
                for candidate, name in ordered:
                    if candidate > address:
                        break
                    base = (candidate, name)
            if base is None:
                stderr_write("Daddr not a proc nor a dlabel: " + data.addr)
                return 82
            data.addr = base[1]
            data.addr_offset = _s32(data.addr_offset + (address - base[0]))

    return None
