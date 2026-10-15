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

"""FPU stack emulation helpers."""

from .emit import add_output_line, get_load_type, get_new_temporary_register, get_parameter_read_value, store_temporary_register_to_reg
from .state import G, Param
from .tables import EMPTY, NUM_FLOAT_REGS, READ, WRITE


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
