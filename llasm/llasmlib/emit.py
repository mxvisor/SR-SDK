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

"""Output lines, temporaries, registers and operand access."""

from .expr import _s32
from .source import write_error2
from .state import G
from .tables import EMPTY, NUM_REGS, READ, REGS64_START_NUM, WRITE, _llref, registers_base_list


def add_output_line(line):
    G.output_lines.append(line)


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
