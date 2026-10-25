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

"""Translation of a procedure body."""

import os

from .emit import add_output_line, get_addr_to_int32, get_int32_to_ptr, get_load_type, get_new_label, get_new_temporary_register, get_parameter_read_addr, get_parameter_read_value, store_temporary_register_to_reg, writeback_register, writeback_registers
from .expr import _s32, _to_long, calculate_expr_value, replace_defines_in_expr
from .source import get_current_params, get_current_params2, write_error2
from .state import G, Param, ProcInstr
from .strutil import strip
from .tables import EMPTY, NUM_FLOAT_REGS, NUM_REGS, NUM_REGS64, WRITE, _llref, registers64_base_list, registers_base_list

LEGACY_TEMPS = os.environ.get("LLASM_LEGACY_TEMPS") == "1"
from .x87 import _BINARY_FLOAT, _COMPARE_FLOAT, _FLOAT_UNARY, binary_float_instr, compare_float_instr, get_float_reg_value, increase_float_stack_top, store_temporary_register_to_float_reg, writeback_float_register, writeback_float_registers


_ONE_REG_WRITE0 = ("mov", "add", "sub", "shl", "lshr", "ashr", "and", "or", "xor", "mul",
                   "load", "load8s", "load8z", "load16s", "load16z",
                   "ext8s", "ext16s", "ins16", "ins8ll", "ins8lh", "ins8hl", "ins8hh",
                   "ctlz", "bswap", "not")


_FLOAT_WRITE_TOP0 = ("FCHS_VOID", "FIADD_INT32", "FADD_FLOAT", "FISUB_INT32", "FISUBR_INT32",
                     "FSUB_FLOAT", "FSUBR_FLOAT", "FIMUL_INT32", "FMUL_FLOAT", "FDIV_FLOAT",
                     "FDIVR_FLOAT", "FIDIV_INT32", "FIDIVR_INT32", "FSQRT_VOID", "FSIN_VOID",
                     "FCOS_VOID", "FLOG_VOID", "FLOG10_VOID", "FABS_VOID", "FROUND_VOID")


class _ProcCtx:
    """The state of one procedure body pass. The analysis and the emission use the
    same attributes; the handlers below are called with this object."""
    pass


def _find_handler(table, cache, word, default):
    handler = cache.get(word)
    if handler is None:
        handler = default
        for words, candidate in table:
            if word in words:
                handler = candidate
                break
        cache[word] = handler
    return handler


def _unhandled(c):
    write_error2("Unhandled instruction: " + c.current_line.orig)
    return False


def _analyze_one_reg_write0(c):
        c.info.write_reg[0] = c.paramvals[0].value


def _analyze_store(c):
        pass


def _analyze_tcall(c):
        if c.linenum + 1 != len(c.curproc.lines):
            write_error2("Instructions after tcall: " + c.current_line.orig)
            return False


def _analyze_ctcallz(c):
        if c.linenum + 2 != len(c.curproc.lines) or c.curproc.lines[c.linenum + 1].word != "tcall":
            write_error2("Instructions after ctcall: " + c.current_line.orig)
            return False


def _analyze_cmovz(c):
        c.info.write_reg[0] = c.paramvals[1].value


def _analyze_cmoveq(c):
        c.info.write_reg[0] = c.paramvals[2].value


def _analyze_call(c):
        if c.info.func_has_return_value:
            c.info.write_reg[0] = "tmp0"
        if c.is_inside_if:
            c.if_contains_call = True


def _analyze_imul(c):
        c.info.write_reg[0] = c.paramvals[0].value
        c.info.write_reg[1] = c.paramvals[1].value


def _analyze_ifz(c):
        if c.is_inside_if:
            write_error2("If inside if: " + c.current_line.orig)
            return False

        c.is_inside_if = True
        c.is_static_if = False
        c.if_contains_call = False
        c.if_float_stack_top = c.current_float_stack_top

        if G.procedure_optimization_level >= 1:
            if c.linenum > 0 and c.curproc.lines[c.linenum - 1].word == "mov":
                prev = c.proc_instr_info[c.linenum - 1]
                if prev.params[0].value == c.paramvals[0].value and prev.params[1].type == "const":
                    c.is_static_if = True
                    value = prev.params[1].ivalue

                    if c.current_line.word == "ifz":
                        c.static_if_result = (value == 0)
                    else:
                        c.static_if_result = (value != 0)

            if c.linenum > 1 and c.curproc.lines[c.linenum - 2].word == "mov" and c.curproc.lines[c.linenum - 1].word == "cmovslt":
                prev2 = c.proc_instr_info[c.linenum - 2]
                prev1 = c.proc_instr_info[c.linenum - 1]
                if prev2.params[0].value == c.paramvals[0].value and prev2.params[1].type == "const":
                    if (prev1.params[0].value == c.paramvals[0].value and
                            prev1.params[1].type == "const" and
                            prev1.params[2].value == c.paramvals[0].value and
                            prev1.params[3].type == "const" and
                            prev1.params[4].type == "const"):
                        c.is_static_if = True
                        value2 = prev2.params[1].ivalue
                        value1 = prev1.params[1].ivalue

                        if value2 < value1:
                            value0 = prev1.params[3].ivalue
                        else:
                            value0 = prev1.params[4].ivalue

                        if c.current_line.word == "ifz":
                            c.static_if_result = (value0 == 0)
                        else:
                            c.static_if_result = (value0 != 0)

        if G.procedure_optimization_level > 1 and not c.is_static_if:
            G.procedure_optimization_level = 1

        if c.is_static_if:
            c.info.is_valid = False


def _analyze_endif(c):
        if not c.is_inside_if:
            write_error2("Endif without if: " + c.current_line.orig)
            return False

        c.is_inside_if = False
        c.info.is_inside_if = False
        c.info.if_contains_call = c.if_contains_call
        c.info.if_changes_st_top = (c.current_float_stack_top != c.if_float_stack_top)

        if c.is_static_if:
            c.info.is_valid = False

            if not c.static_if_result:
                c.current_float_stack_top = c.if_float_stack_top


def _analyze_div_64_ze(c):
        c.info.write_reg[0] = "eax"
        c.info.write_reg[1] = "edx"


def _analyze_fldcw_uint16(c):
        c.info.write_reg[0] = "st_cw"


def _analyze_fnstcw_void(c):
        c.info.write_reg[0] = "tmp0"
        c.used_reg_list["st_cw"] = True


def _analyze_fnstsw_void(c):
        c.info.write_reg[0] = "tmp0"
        c.used_reg_list["st_sw_cond"] = True
        c.used_reg_list["st_top"] = True


def _analyze_float_write_top0(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_float_reg[0] = c.current_float_stack_top
        c.used_reg_list["st_top"] = True


def _analyze_ftan_void(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.info.write_float_reg[0] = c.current_float_stack_top
        c.current_float_stack_top = (c.current_float_stack_top + 7) & 7
        c.info.write_float_reg[1] = c.current_float_stack_top


def _analyze_fxch_st(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_float_reg[0] = c.current_float_stack_top
        c.info.write_float_reg[1] = (c.current_float_stack_top + c.paramvals[0].ivalue) & 7
        c.used_reg_list["st_top"] = True


def _analyze_fst_st(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_float_reg[0] = (c.current_float_stack_top + c.paramvals[0].ivalue) & 7
        c.used_reg_list["st_top"] = True
        c.used_float_reg_list[c.current_float_stack_top] = True


def _analyze_fadd_st(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_float_reg[0] = c.current_float_stack_top
        c.used_reg_list["st_top"] = True
        c.used_float_reg_list[(c.current_float_stack_top + c.paramvals[0].ivalue) & 7] = True


def _analyze_fcom_float(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.used_reg_list["st_top"] = True
        c.used_float_reg_list[c.current_float_stack_top] = True


def _analyze_fcom_st(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.used_reg_list["st_top"] = True
        c.used_float_reg_list[c.current_float_stack_top] = True
        c.used_float_reg_list[(c.current_float_stack_top + c.paramvals[0].ivalue) & 7] = True


def _analyze_fild_int32(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.current_float_stack_top = (c.current_float_stack_top + 7) & 7
        c.info.write_float_reg[0] = c.current_float_stack_top


def _analyze_fld_st(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.used_float_reg_list[(c.current_float_stack_top + c.paramvals[0].ivalue) & 7] = True

        c.current_float_stack_top = (c.current_float_stack_top + 7) & 7
        c.info.write_float_reg[0] = c.current_float_stack_top


def _analyze_fstp_st(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.info.write_float_reg[0] = (c.current_float_stack_top + c.paramvals[0].ivalue) & 7
        c.used_float_reg_list[c.current_float_stack_top] = True

        c.current_float_stack_top = (c.current_float_stack_top + 1) & 7


def _analyze_fcomp_float(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.used_float_reg_list[c.current_float_stack_top] = True

        c.current_float_stack_top = (c.current_float_stack_top + 1) & 7


def _analyze_fcomp_st(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.used_float_reg_list[c.current_float_stack_top] = True
        c.used_float_reg_list[(c.current_float_stack_top + c.paramvals[0].ivalue) & 7] = True

        c.current_float_stack_top = (c.current_float_stack_top + 1) & 7


def _analyze_fmod_void(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.info.write_float_reg[0] = (c.current_float_stack_top + 1) & 7
        c.used_float_reg_list[c.current_float_stack_top] = True

        c.current_float_stack_top = (c.current_float_stack_top + 1) & 7


def _analyze_fcompp_void(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[1] = "st_top"
        c.used_float_reg_list[c.current_float_stack_top] = True
        c.used_float_reg_list[(c.current_float_stack_top + 1) & 7] = True

        c.current_float_stack_top = (c.current_float_stack_top + 2) & 7


def _analyze_fst_float(c):
        c.info.write_reg[0] = "st_sw_cond"
        c.info.write_reg[2] = "tmp0"
        c.used_reg_list["st_top"] = True
        c.used_float_reg_list[c.current_float_stack_top] = True

        if c.current_line.word == "FSTP_FLOAT":
            c.info.write_reg[1] = "st_top"
            c.current_float_stack_top = (c.current_float_stack_top + 1) & 7


def _emit_mov(c):
        src = get_parameter_read_value(c.paramvals[1])
        store_temporary_register_to_reg(src, c.paramvals[0].value, c.last_reg_write[0])


def _emit_add(c):
        src1 = get_parameter_read_value(c.paramvals[1])
        src2 = get_parameter_read_value(c.paramvals[2])
        dst = get_new_temporary_register()

        add_output_line(dst + " = " + c.current_line.word + " i32 " + src1 + ", " + src2)

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_load(c):
        src = get_parameter_read_addr(c.paramvals[1], "i32")
        dst = get_new_temporary_register()

        add_output_line(dst + " = load " + get_load_type("i32") + " " + src + ", align " + c.paramvals[2].value)

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_load8s(c):
        srctype = "i8" if (c.word == "load8s" or c.word == "load8z") else "i16"
        extinstr = "sext" if (c.word == "load8s" or c.word == "load16s") else "zext"

        src = get_parameter_read_addr(c.paramvals[1], srctype)
        tmpdst = get_new_temporary_register()
        dst = get_new_temporary_register()

        add_output_line(tmpdst + " = load " + get_load_type(srctype) + " " + src + ", align " + c.paramvals[2].value)
        add_output_line(dst + " = " + extinstr + " " + srctype + " " + tmpdst + " to i32")

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_store(c):
        src = get_parameter_read_value(c.paramvals[0])
        dst = get_parameter_read_addr(c.paramvals[1], "i32")

        add_output_line("store i32 " + src + ", i32* " + dst + ", align " + c.paramvals[2].value)


def _emit_store8(c):
        srctype = "i8" if c.word == "store8" else "i16"

        src = get_parameter_read_value(c.paramvals[0])
        dst = get_parameter_read_addr(c.paramvals[1], srctype)
        src2 = get_new_temporary_register()

        add_output_line(src2 + " = trunc i32 " + src + " to " + srctype)
        add_output_line("store " + srctype + " " + src2 + ", " + srctype + "* " + dst + ", align " + c.paramvals[2].value)


def _emit_ext8s(c):
        src = get_parameter_read_value(c.paramvals[1])
        src2 = get_new_temporary_register()
        dst = get_new_temporary_register()

        srctype = "i8" if c.word == "ext8s" else "i16"

        add_output_line(src2 + " = trunc i32 " + src + " to " + srctype)
        add_output_line(dst + " = sext " + srctype + " " + src2 + " to i32")

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_ins16(c):
        src1 = get_parameter_read_value(c.paramvals[1])
        src2 = get_parameter_read_value(c.paramvals[2])
        src21 = get_new_temporary_register()
        src22 = get_new_temporary_register()
        src11 = get_new_temporary_register()
        dst = get_new_temporary_register()

        srctype = "i16" if c.word == "ins16" else "i8"
        andvalue = 0xffff0000 if c.word == "ins16" else 0xffffff00

        add_output_line(src21 + " = trunc i32 " + src2 + " to " + srctype)
        add_output_line(src22 + " = zext " + srctype + " " + src21 + " to i32")
        add_output_line(src11 + " = and i32 " + src1 + ", " + str(andvalue))
        add_output_line(dst + " = or i32 " + src11 + ", " + src22)

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_ins8lh(c):
        src1 = get_parameter_read_value(c.paramvals[1])
        src2 = get_parameter_read_value(c.paramvals[2])
        src21 = get_new_temporary_register()
        src22 = get_new_temporary_register()
        src23 = get_new_temporary_register()
        src11 = get_new_temporary_register()
        dst = get_new_temporary_register()

        if c.word == "ins8lh":
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

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_ins8hh(c):
        src1 = get_parameter_read_value(c.paramvals[1])
        src2 = get_parameter_read_value(c.paramvals[2])
        src21 = get_new_temporary_register()
        src11 = get_new_temporary_register()
        dst = get_new_temporary_register()

        add_output_line(src21 + " = and i32 " + src2 + ", " + str(0xff00))
        add_output_line(src11 + " = and i32 " + src1 + ", " + str(0xffff00ff))
        add_output_line(dst + " = or i32 " + src11 + ", " + src21)

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_tcall(c):
        if G.procedure_optimization_level >= 1:
            writeback_registers()
            writeback_float_registers(c.current_float_stack_top)

        if G.no_tail_calls:
            addr = get_parameter_read_value(c.paramvals[0], True)
            add_output_line("ret i8* " + addr)
        else:
            if c.paramvals[0].type == "procaddr":
                add_output_line("musttail call fastcc void " + _llref(c.paramvals[0].value) + "(%_cpu* %cpu) nounwind")
            else:
                dst = get_parameter_read_value(c.paramvals[0])
                addr = get_int32_to_ptr(dst, "void(%_cpu*)")

                add_output_line("musttail call fastcc void " + addr + "(%_cpu* %cpu) nounwind")


def _emit_ctcallz(c):
        if G.procedure_optimization_level >= 1:
            writeback_registers()
            writeback_float_registers(c.current_float_stack_top)

        condreg = get_parameter_read_value(c.paramvals[0])
        cond = get_new_temporary_register()
        labelt = get_new_label()
        labelf = get_new_label()

        condtype = "eq" if c.word == "ctcallz" else "ne"

        add_output_line(cond + " = icmp " + condtype + " i32 " + condreg + ", 0")
        add_output_line("br i1 " + cond + ", label %" + labelt + ", label %" + labelf)
        add_output_line(labelt + ":")

        if G.no_tail_calls:
            addr = get_parameter_read_value(c.paramvals[1], True)
            add_output_line("ret i8* " + addr)
        else:
            add_output_line("musttail call fastcc void " + _llref(c.paramvals[1].value) + "(%_cpu* %cpu) nounwind")

            add_output_line("ret void")
        add_output_line(labelf + ":")


def _emit_cmovz(c):
        condreg = get_parameter_read_value(c.paramvals[0])
        src1 = get_parameter_read_value(c.paramvals[2])
        src2 = get_parameter_read_value(c.paramvals[3])
        cond = get_new_temporary_register()
        dst = get_new_temporary_register()

        add_output_line(cond + " = icmp eq i32 " + condreg + ", 0")
        add_output_line(dst + " = select i1 " + cond + ", i32 " + src1 + ", i32 " + src2)

        store_temporary_register_to_reg(dst, c.paramvals[1].value, c.last_reg_write[0])


def _emit_cmoveq(c):
        condreg1 = get_parameter_read_value(c.paramvals[0])
        condreg2 = get_parameter_read_value(c.paramvals[1])
        src1 = get_parameter_read_value(c.paramvals[3])
        src2 = get_parameter_read_value(c.paramvals[4])
        cond = get_new_temporary_register()
        dst = get_new_temporary_register()

        condtype = c.word[4:]

        add_output_line(cond + " = icmp " + condtype + " i32 " + condreg1 + ", " + condreg2)
        add_output_line(dst + " = select i1 " + cond + ", i32 " + src1 + ", i32 " + src2)

        store_temporary_register_to_reg(dst, c.paramvals[2].value, c.last_reg_write[0])


def _emit_call(c):
        if G.procedure_optimization_level >= 1:
            writeback_registers()
            writeback_float_registers(c.current_float_stack_top)

        func = G.func_list[c.current_line.param1]

        src = [None] * len(c.paramvals)
        for i in range(len(c.paramvals)):
            src[i] = get_parameter_read_value(c.paramvals[i], (func.params[i].type == 1), True)

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
            # llasm's own temp registers (tmpadr/tmpcnd/tmp0..) are not guest
            # state: their SSA values survive a C call, and SR can legally
            # schedule a `call` between e.g. a cmoveq-to-tmpcnd and the ctcallz
            # that consumes it.  Keep them cached; only guest registers go.
            # (LLASM_LEGACY_TEMPS=1 gives llasm.d's behaviour: the tests that
            # compare with the D build set it.)
            saved_temps = {}
            if G.procedure_optimization_level >= 2 and not LEGACY_TEMPS:
                for name in G.temp_regs_list:
                    if name in G.current_temporary_register:
                        saved_temps[name] = G.current_temporary_register[name]
            G.current_temporary_register = saved_temps

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

                store_temporary_register_to_reg(dst2, "tmp0", c.last_reg_write[0])
            else:
                store_temporary_register_to_reg(dst, "tmp0", c.last_reg_write[0])
        else:
            add_output_line("call void " + _llref(func.real_name) + "(" + srcstr + ") nounwind")


def _emit_imul(c):
        src1 = get_parameter_read_value(c.paramvals[2])
        src2 = get_parameter_read_value(c.paramvals[3])
        src11 = get_new_temporary_register()
        src21 = get_new_temporary_register()
        dst = get_new_temporary_register()
        dstlo = get_new_temporary_register()
        tmpdst = get_new_temporary_register()
        dsthi = get_new_temporary_register()

        extinstr = "sext" if c.word == "imul" else "zext"

        add_output_line(src11 + " = " + extinstr + " i32 " + src1 + " to i64")
        add_output_line(src21 + " = " + extinstr + " i32 " + src2 + " to i64")
        add_output_line(dst + " = mul i64 " + src11 + ", " + src21)
        add_output_line(dstlo + " = trunc i64 " + dst + " to i32")
        add_output_line(tmpdst + " = lshr i64 " + dst + ", 32")
        add_output_line(dsthi + " = trunc i64 " + tmpdst + " to i32")

        store_temporary_register_to_reg(dstlo, c.paramvals[0].value, c.last_reg_write[0])
        store_temporary_register_to_reg(dsthi, c.paramvals[1].value, c.last_reg_write[1])


def _emit_ifz(c):
        if G.procedure_optimization_level >= 1:
            if c.proc_instr_info[c.linenum].if_contains_call:
                writeback_registers()
            if c.proc_instr_info[c.linenum].if_contains_call or c.proc_instr_info[c.linenum].if_changes_st_top:
                writeback_float_registers(c.current_float_stack_top)

        condreg = get_parameter_read_value(c.paramvals[0])
        cond = get_new_temporary_register()
        labelt = get_new_label()
        labelf = get_new_label()

        condtype = "eq" if c.word == "ifz" else "ne"

        if G.procedure_optimization_level >= 1:
            c.if_reg_state = list(G.register_state)
            c.if_float_reg_state = list(G.float_reg_state)
            c.if_reg64_state = list(G.register64_state)

        add_output_line(cond + " = icmp " + condtype + " i32 " + condreg + ", 0")
        add_output_line("br i1 " + cond + ", label %" + labelt + ", label %" + labelf)
        add_output_line(labelt + ":")

        c.endif_label = labelf


def _emit_endif(c):
        if G.procedure_optimization_level >= 1:
            for i in range(NUM_REGS):
                if G.register_state[i] == c.if_reg_state[i]:
                    continue

                if G.register_state[i] == WRITE:
                    writeback_register(i)

                if c.if_reg_state[i] == WRITE:
                    if G.register_state[i] == EMPTY:
                        get_parameter_read_value(Param("reg", registers_base_list[i]))
                    G.register_state[i] = WRITE

            if c.proc_instr_info[c.linenum].if_contains_call or c.proc_instr_info[c.linenum].if_changes_st_top:
                writeback_float_registers(c.current_float_stack_top)

                for i in range(NUM_FLOAT_REGS):
                    G.float_reg_state[i] = EMPTY
                G.current_temporary_float_register = {}
            else:
                top = ""

                for i in range(NUM_FLOAT_REGS):
                    if G.float_reg_state[i] == c.if_float_reg_state[i]:
                        continue

                    if G.float_reg_state[i] == WRITE:
                        if top == "":
                            top = get_parameter_read_value(Param("reg", "st_top"))
                        writeback_float_register(i, top, c.current_float_stack_top)

                    if G.float_reg_state[i] != c.if_float_reg_state[i]:
                        G.float_reg_state[i] = EMPTY

            for i in range(NUM_REGS):
                if G.register_state[i] != c.if_reg_state[i]:
                    G.register_state[i] = EMPTY

            for i in range(NUM_REGS64):
                if G.register64_state[i] != c.if_reg64_state[i]:
                    G.register64_state[i] = EMPTY

        add_output_line("br label %" + c.endif_label)
        add_output_line(c.endif_label + ":")


def _emit_ctlz(c):
        src = get_parameter_read_value(c.paramvals[1])
        dst = get_new_temporary_register()

        add_output_line(dst + " = call i32 @llvm.ctlz.i32(i32 " + src + ", i1 0)")

        store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_bswap(c):
    src = get_parameter_read_value(c.paramvals[1])
    dst = get_new_temporary_register()

    add_output_line(dst + " = call i32 @llvm.bswap.i32(i32 " + src + ")")

    store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_not(c):
    # not reg: the bitwise complement of the register, in the register
    src = get_parameter_read_value(c.paramvals[0])
    dst = get_new_temporary_register()

    add_output_line(dst + " = xor i32 " + src + ", -1")

    store_temporary_register_to_reg(dst, c.paramvals[0].value, c.last_reg_write[0])


def _emit_div_64_ze(c):
        src1 = get_parameter_read_value(Param("reg", "eax"))
        src2 = get_parameter_read_value(c.paramvals[0])
        dst1 = get_new_temporary_register()
        dst2 = get_new_temporary_register()

        prefix = "u" if c.word == "DIV_64_ZE" else "s"

        add_output_line(dst1 + " = " + prefix + "div i32 " + src1 + ", " + src2)
        add_output_line(dst2 + " = " + prefix + "rem i32 " + src1 + ", " + src2)

        store_temporary_register_to_reg(dst1, "eax", c.last_reg_write[0])
        store_temporary_register_to_reg(dst2, "edx", c.last_reg_write[1])


def _emit_fldcw_uint16(c):
        src = get_parameter_read_value(c.paramvals[0])
        dst = get_new_temporary_register()

        add_output_line(dst + " = and i32 " + src + ", " + str(0xffff))

        store_temporary_register_to_reg(dst, "st_cw", c.last_reg_write[0])


def _emit_fnstcw_void(c):
        src = get_parameter_read_value(Param("reg", "st_cw"))

        store_temporary_register_to_reg(src, "tmp0", c.last_reg_write[0])


def _emit_fnstsw_void(c):
        src1 = get_parameter_read_value(Param("reg", "st_top"))
        src2 = get_parameter_read_value(Param("reg", "st_sw_cond"))
        dst1 = get_new_temporary_register()
        dst2 = get_new_temporary_register()

        add_output_line(dst1 + " = shl i32 " + src1 + ", 11")
        add_output_line(dst2 + " = add i32 " + src2 + ", " + dst1)

        store_temporary_register_to_reg(dst2, "tmp0", c.last_reg_write[0])


def _emit_fchs_void(c):
        top = get_parameter_read_value(Param("reg", "st_top"))
        src = get_float_reg_value(top, 0, c.current_float_stack_top)
        dst = get_new_temporary_register()

        add_output_line(dst + " = fneg double " + src)

        store_temporary_register_to_float_reg(dst, top, 0, c.current_float_stack_top, c.last_float_reg_write[0])
        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


def _emit_fxch_st(c):
        top = get_parameter_read_value(Param("reg", "st_top"))
        index = c.paramvals[0].ivalue
        src1 = get_float_reg_value(top, 0, c.current_float_stack_top)
        src2 = get_float_reg_value(top, index, c.current_float_stack_top)

        store_temporary_register_to_float_reg(src1, top, index, c.current_float_stack_top, c.last_float_reg_write[1])
        store_temporary_register_to_float_reg(src2, top, 0, c.current_float_stack_top, c.last_float_reg_write[0])
        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


def _emit_fild_int32(c):
        top1 = get_parameter_read_value(Param("reg", "st_top"))

        if c.word == "FILD_INT32":
            src = get_parameter_read_value(c.paramvals[0])
            dst = get_new_temporary_register()
            add_output_line(dst + " = sitofp i32 " + src + " to double")
        elif c.word == "FLD_FLOAT":
            src = get_parameter_read_value(c.paramvals[0])
            tmp1 = get_new_temporary_register()
            dst = get_new_temporary_register()
            add_output_line(tmp1 + " = bitcast i32 " + src + " to float")
            add_output_line(dst + " = fpext float " + tmp1 + " to double")
        elif c.word == "FLD_ST":
            index = c.paramvals[0].ivalue
            dst = get_float_reg_value(top1, index, c.current_float_stack_top)
        elif c.word == "FLD1_VOID":
            dst = "1.0"
        elif c.word == "FLDLG2_VOID":
            dst = "0.30102999566398119521"
        elif c.word == "FLDLN2_VOID":
            dst = "0.69314718055994530942"
        else:
            dst = "0.0"

        tmp2 = get_new_temporary_register()
        top2 = get_new_temporary_register()

        add_output_line(tmp2 + " = add i32 " + top1 + ", 7")
        add_output_line(top2 + " = and i32 " + tmp2 + ", 7")

        store_temporary_register_to_float_reg(dst, top2, 0, (c.current_float_stack_top + 7) & 7, c.last_float_reg_write[0])
        store_temporary_register_to_reg(top2, "st_top", c.last_reg_write[1])
        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


def _emit_fst_float(c):
        top = get_parameter_read_value(Param("reg", "st_top"))

        if c.word == "FST_ST" or c.word == "FSTP_ST":
            index = c.paramvals[0].ivalue
            if index != 0:
                src = get_float_reg_value(top, 0, c.current_float_stack_top)
                store_temporary_register_to_float_reg(src, top, index, c.current_float_stack_top, c.last_float_reg_write[0])
        else:
            src = get_float_reg_value(top, 0, c.current_float_stack_top)
            tmp = get_new_temporary_register()
            dst = get_new_temporary_register()

            add_output_line(tmp + " = fptrunc double " + src + " to float")
            add_output_line(dst + " = bitcast float " + tmp + " to i32")
            store_temporary_register_to_reg(dst, "tmp0", c.last_reg_write[2])

        if c.word == "FSTP_FLOAT" or c.word == "FSTP_ST":
            increase_float_stack_top(top, 1, c.last_reg_write[1], c.current_float_stack_top)

        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


def _emit_binary_float(c):
        instr, ptype, rparam, rresult, pop = _BINARY_FLOAT[c.word]
        binary_float_instr(instr, ptype, rparam, rresult, pop, c.paramvals[0], c.last_reg_write[0], c.last_reg_write[1], c.current_float_stack_top, c.last_float_reg_write[0])


def _emit_compare_float(c):
        ptype, pop = _COMPARE_FLOAT[c.word]
        compare_float_instr(ptype, pop, c.paramvals[0], c.last_reg_write[0], c.last_reg_write[1], c.current_float_stack_top)


def _emit_fcompp_void(c):
        compare_float_instr(0, 2, Param("const", "1", 1), c.last_reg_write[0], c.last_reg_write[1], c.current_float_stack_top)


def _emit_fmod_void(c):
        binary_float_instr("frem", 0, False, True, True, Param("const", "1", 1), c.last_reg_write[0], c.last_reg_write[1], c.current_float_stack_top, c.last_float_reg_write[0])


def _emit_fmodr_void(c):
        binary_float_instr("frem", 0, True, True, True, Param("const", "1", 1), c.last_reg_write[0], c.last_reg_write[1], c.current_float_stack_top, c.last_float_reg_write[0])


def _emit_float_unary(c):
        top = get_parameter_read_value(Param("reg", "st_top"))
        src = get_float_reg_value(top, 0, c.current_float_stack_top)
        dst = get_new_temporary_register()

        instr = _FLOAT_UNARY[c.word]

        add_output_line(dst + " = call double @llvm." + instr + ".f64(double " + src + ")")

        store_temporary_register_to_float_reg(dst, top, 0, c.current_float_stack_top, c.last_float_reg_write[0])
        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


def _emit_ftan_void(c):
        top1 = get_parameter_read_value(Param("reg", "st_top"))
        src = get_float_reg_value(top1, 0, c.current_float_stack_top)
        dst = get_new_temporary_register()

        add_output_line(dst + " = call double @llvm.tan.f64(double " + src + ")")

        store_temporary_register_to_float_reg(dst, top1, 0, c.current_float_stack_top, c.last_float_reg_write[0])

        tmp2 = get_new_temporary_register()
        top2 = get_new_temporary_register()

        add_output_line(tmp2 + " = add i32 " + top1 + ", 7")
        add_output_line(top2 + " = and i32 " + tmp2 + ", 7")

        store_temporary_register_to_float_reg("1.0", top2, 0, (c.current_float_stack_top + 7) & 7, c.last_float_reg_write[1])
        store_temporary_register_to_reg(top2, "st_top", c.last_reg_write[1])
        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


def _emit_fpow_void(c):
        top = get_parameter_read_value(Param("reg", "st_top"))
        src1 = get_float_reg_value(top, 0, c.current_float_stack_top)
        src2 = get_float_reg_value(top, 1, c.current_float_stack_top)
        dst = get_new_temporary_register()

        reverse_params = (c.word == "FPOWR_VOID")
        add_output_line(dst + " = call double @llvm.pow.f64(double " + (src2 if reverse_params else src1) + ", double " + (src1 if reverse_params else src2) + ")")

        store_temporary_register_to_float_reg(dst, top, 1, c.current_float_stack_top, c.last_float_reg_write[0])
        increase_float_stack_top(top, 1, c.last_reg_write[1], c.current_float_stack_top)
        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


def _emit_fyl2x_void(c):
        top = get_parameter_read_value(Param("reg", "st_top"))
        src1 = get_float_reg_value(top, 0, c.current_float_stack_top)
        tmp = get_new_temporary_register()

        add_output_line(tmp + " = call double @llvm.log2.f64(double " + src1 + ")")

        src2 = get_float_reg_value(top, 1, c.current_float_stack_top)
        dst = get_new_temporary_register()

        add_output_line(dst + " = fmul double " + src2 + ", " + tmp)

        store_temporary_register_to_float_reg(dst, top, 1, c.current_float_stack_top, c.last_float_reg_write[0])
        increase_float_stack_top(top, 1, c.last_reg_write[1], c.current_float_stack_top)
        store_temporary_register_to_reg("0", "st_sw_cond", c.last_reg_write[0])


_ANALYZE_TABLE = [
    (_ONE_REG_WRITE0, _analyze_one_reg_write0),
    (("store", "store8", "store16"), _analyze_store),
    (("tcall",), _analyze_tcall),
    (("ctcallz", "ctcallnz"), _analyze_ctcallz),
    (("cmovz",), _analyze_cmovz),
    (("cmoveq", "cmovult", "cmovugt", "cmovslt", "cmovsgt"), _analyze_cmoveq),
    (("call",), _analyze_call),
    (("imul", "umul"), _analyze_imul),
    (("ifz", "ifnz"), _analyze_ifz),
    (("endif",), _analyze_endif),
    (("DIV_64_ZE", "IDIV_64_SE"), _analyze_div_64_ze),
    (("FLDCW_UINT16",), _analyze_fldcw_uint16),
    (("FNSTCW_VOID",), _analyze_fnstcw_void),
    (("FNSTSW_VOID",), _analyze_fnstsw_void),
    (_FLOAT_WRITE_TOP0, _analyze_float_write_top0),
    (("FTAN_VOID",), _analyze_ftan_void),
    (("FXCH_ST",), _analyze_fxch_st),
    (("FST_ST", "FADD_TO_ST", "FSUB_TO_ST", "FMUL_TO_ST", "FDIV_TO_ST"), _analyze_fst_st),
    (("FADD_ST", "FSUB_ST", "FSUBR_ST", "FMUL_ST", "FDIV_ST", "FDIVR_ST"), _analyze_fadd_st),
    (("FCOM_FLOAT",), _analyze_fcom_float),
    (("FCOM_ST", "FUCOM_ST"), _analyze_fcom_st),
    (("FILD_INT32", "FLD_FLOAT", "FLD1_VOID", "FLDLG2_VOID", "FLDLN2_VOID", "FLDZ_VOID"), _analyze_fild_int32),
    (("FLD_ST",), _analyze_fld_st),
    (("FSTP_ST", "FADDP_ST", "FSUBP_ST", "FSUBRP_ST", "FMULP_ST", "FDIVP_ST", "FDIVRP_ST"), _analyze_fstp_st),
    (("FCOMP_FLOAT",), _analyze_fcomp_float),
    (("FCOMP_ST", "FUCOMP_ST"), _analyze_fcomp_st),
    (("FMOD_VOID", "FMODR_VOID", "FPOW_VOID", "FPOWR_VOID", "FYL2X_VOID"), _analyze_fmod_void),
    (("FCOMPP_VOID", "FUCOMPP_VOID"), _analyze_fcompp_void),
    (("FST_FLOAT", "FSTP_FLOAT"), _analyze_fst_float),
]
_ANALYZE_CACHE = {}


_EMIT_TABLE = [
    (("mov",), _emit_mov),
    (("add", "sub", "shl", "lshr", "ashr", "and", "or", "xor", "mul"), _emit_add),
    (("load",), _emit_load),
    (("load8s", "load8z", "load16s", "load16z"), _emit_load8s),
    (("store",), _emit_store),
    (("store8", "store16"), _emit_store8),
    (("ext8s", "ext16s"), _emit_ext8s),
    (("ins16", "ins8ll"), _emit_ins16),
    (("ins8lh", "ins8hl"), _emit_ins8lh),
    (("ins8hh",), _emit_ins8hh),
    (("tcall",), _emit_tcall),
    (("ctcallz", "ctcallnz"), _emit_ctcallz),
    (("cmovz",), _emit_cmovz),
    (("cmoveq", "cmovult", "cmovugt", "cmovslt", "cmovsgt"), _emit_cmoveq),
    (("call",), _emit_call),
    (("imul", "umul"), _emit_imul),
    (("ifz", "ifnz"), _emit_ifz),
    (("endif",), _emit_endif),
    (("ctlz",), _emit_ctlz),
    (("bswap",), _emit_bswap),
    (("not",), _emit_not),
    (("DIV_64_ZE", "IDIV_64_SE"), _emit_div_64_ze),
    (("FLDCW_UINT16",), _emit_fldcw_uint16),
    (("FNSTCW_VOID",), _emit_fnstcw_void),
    (("FNSTSW_VOID",), _emit_fnstsw_void),
    (("FCHS_VOID",), _emit_fchs_void),
    (("FXCH_ST",), _emit_fxch_st),
    (("FILD_INT32", "FLD_FLOAT", "FLD_ST", "FLD1_VOID", "FLDLG2_VOID", "FLDLN2_VOID", "FLDZ_VOID"), _emit_fild_int32),
    (("FST_FLOAT", "FST_ST", "FSTP_FLOAT", "FSTP_ST"), _emit_fst_float),
    (_BINARY_FLOAT, _emit_binary_float),
    (_COMPARE_FLOAT, _emit_compare_float),
    (("FCOMPP_VOID", "FUCOMPP_VOID"), _emit_fcompp_void),
    (("FMOD_VOID",), _emit_fmod_void),
    (("FMODR_VOID",), _emit_fmodr_void),
    (_FLOAT_UNARY, _emit_float_unary),
    (("FTAN_VOID",), _emit_ftan_void),
    (("FPOW_VOID", "FPOWR_VOID"), _emit_fpow_void),
    (("FYL2X_VOID",), _emit_fyl2x_void),
]
_EMIT_CACHE = {}



def _analyze(c):
    """Pass 1: check every instruction, find the registers it writes and the if/endif structure."""
    if len(c.curproc.lines) == 0:
        write_error2("Empty procedure")
        return False

    c.proc_instr_info = [ProcInstr() for _ in c.curproc.lines]

    G.procedure_optimization_level = G.global_optimization_level

    # ------------------------------------------------------------------
    # analyze / check pass

    c.current_float_stack_top = 0

    if G.pointers_with_offset:
        c.used_reg64_list["ptrofs"] = True

    for linenum in range(len(c.curproc.lines)):
        c.linenum = linenum
        G.current_line = c.curproc.lines[c.linenum]
        c.current_line = G.current_line
        instruction = G.instructions_list[c.current_line.word]
        c.info = c.proc_instr_info[c.linenum]

        c.info.is_valid = True
        c.info.is_inside_if = False
        c.info.func_has_return_value = False
        c.info.if_contains_call = False
        c.info.if_changes_st_top = False
        c.info.float_stack_top = c.current_float_stack_top
        c.info.write_reg = ["", "", ""]
        c.info.write_float_reg = [-1, -1]
        c.info.last_reg_write = [False, False, False]
        c.info.last_float_reg_write = [False, False]
        c.info.params = []

        if len(instruction.variable_params2) != 0:
            if c.current_line.word == "call" and len(instruction.params) == 1:
                if c.current_line.param1 not in G.func_list:
                    write_error2("Function not found: " + c.current_line.param1)
                    return False

                func = G.func_list[c.current_line.param1]

                c.info.func_has_return_value = func.has_return_value

                params = get_current_params2()

                if len(params) != len(func.params):
                    write_error2("Wrong number of function parameters: " + c.current_line.orig)
                    return False

                for i in range(len(params)):
                    if func.params[i].type == 2:
                        if params[i] != "$":
                            write_error2("Expecting $ in function parameters: " + c.current_line.orig)
                            return False
                    else:
                        if params[i] == "$":
                            write_error2("Unexpected $ in function parameters: " + c.current_line.orig)
                            return False
            else:
                write_error2("Instruction error: " + c.current_line.orig)
                return False
        else:
            params = get_current_params()

        c.paramvals = [None] * len(params)

        # get parameters type and value
        for i in range(len(params)):
            if params[i] == "$":
                c.paramvals[i] = Param("$", "%cpu")
                continue

            if params[i] in G.registers_list:
                c.paramvals[i] = Param("reg", params[i])

                # update list of used registers in procedure
                c.used_reg_list[params[i]] = True

                continue

            if params[i] in G.func_list:
                c.paramvals[i] = Param("funcaddr", G.func_list[params[i]].real_name)
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
                                c.paramvals[i] = Param("externaddr", param_base, _s32(_to_long(displacement)))
                                continue

            if params[i] in G.define_list:
                param = G.define_list[params[i]]
            else:
                param = params[i]

            if param in G.extern_list or param in G.dlabel_list:
                c.paramvals[i] = Param("externaddr", param, 0)
                continue

            if param in G.proc_list:
                c.paramvals[i] = Param("procaddr", param)
                continue

            ok, param = replace_defines_in_expr(param)
            if ok:
                ok, param = calculate_expr_value(param)
                if ok:
                    c.paramvals[i] = Param("const", param, _s32(_to_long(param)))
                    continue

            write_error2("Unknown instruction parameter: " + c.current_line.orig)
            return False

        # check parameters type
        if len(instruction.variable_params2) != 0:
            for pv in c.paramvals:
                if pv.type not in instruction.variable_params2:
                    write_error2("Wrong instruction parameter type: " + c.current_line.orig)
                    return False
        else:
            for i in range(len(c.paramvals)):
                if c.paramvals[i].type not in instruction.params2[i]:
                    write_error2("Wrong instruction parameter type: " + c.current_line.orig)
                    return False

        c.info.params = list(c.paramvals)

        if c.is_inside_if:
            if c.is_static_if:
                if not c.static_if_result:
                    c.info.is_valid = False
            else:
                c.info.is_inside_if = True

        c.word = c.current_line.word
        handler = _find_handler(_ANALYZE_TABLE, _ANALYZE_CACHE, c.word, _unhandled)
        if handler(c) is False:
            return False

        # update list of used registers in procedure
        for i in range(3):
            reg = c.info.write_reg[i]
            if reg != "":
                c.used_reg_list[reg] = True

        # update list of used float registers in procedure
        for i in range(2):
            reg = c.info.write_float_reg[i]
            if reg >= 0:
                c.used_float_reg_list[reg] = True

    if c.is_inside_if:
        write_error2("Missing endif: " + G.current_line.orig)
        return False

    # ------------------------------------------------------------------
    # optimization pass

    return True



def _backward_pass(c):
    """Pass 2: which writes of registers are the last ones (backwards over the procedure)."""
    # ------------------------------------------------------------------
    # optimization pass

    if G.procedure_optimization_level >= 1:
        if_changes_st_top = False
        c.last_reg_write = {}
        c.last_float_reg_write = [True] * NUM_FLOAT_REGS

        for reg in registers_base_list:
            c.last_reg_write[reg] = True

        for linenum in range(len(c.curproc.lines) - 1, -1, -1):
            c.info = c.proc_instr_info[linenum]
            if not c.info.is_valid:
                continue

            instr_word = c.curproc.lines[linenum].word

            if c.is_inside_if:
                if instr_word == "ifz" or instr_word == "ifnz":
                    c.is_inside_if = False
                    c.info.if_contains_call = c.if_contains_call
                    c.info.if_changes_st_top = if_changes_st_top
            else:
                if instr_word == "endif":
                    c.is_inside_if = True
                    c.if_contains_call = c.info.if_contains_call
                    if_changes_st_top = c.info.if_changes_st_top

            for i in range(3):
                reg = c.info.write_reg[i]

                if reg != "":
                    if c.last_reg_write[reg]:
                        if not c.info.is_inside_if:
                            c.info.last_reg_write[i] = True

                    c.last_reg_write[reg] = False
            for i in range(2):
                stack_index = c.info.write_float_reg[i]

                if stack_index >= 0:
                    if c.last_float_reg_write[stack_index]:
                        if not c.info.is_inside_if:
                            c.info.last_float_reg_write[i] = True

                    c.last_float_reg_write[stack_index] = False

            if instr_word == "call" or ((instr_word == "ifz" or instr_word == "ifnz") and c.info.if_contains_call):
                for reg in registers_base_list:
                    c.last_reg_write[reg] = True
            if instr_word == "call" or ((instr_word == "ifz" or instr_word == "ifnz") and (c.info.if_contains_call or c.info.if_changes_st_top)):
                for i in range(NUM_FLOAT_REGS):
                    c.last_float_reg_write[i] = True

    # ------------------------------------------------------------------
    # output pass




def _declare_registers(c):
    """The allocas of the registers the procedure uses."""
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
            if G.procedure_optimization_level == 1 and registers_base_list[i] in c.used_reg_list:
                add_output_line("%" + registers_base_list[i] + " = alloca i32, align 4")

        for i in range(NUM_FLOAT_REGS):
            G.float_reg_state[i] = EMPTY
            if G.procedure_optimization_level == 1 and i in c.used_float_reg_list:
                add_output_line("%st" + str(i) + " = alloca double, align 8")

        for i in range(NUM_REGS64):
            G.register64_state[i] = EMPTY
            if G.procedure_optimization_level == 1 and registers64_base_list[i] in c.used_reg64_list:
                add_output_line("%" + registers64_base_list[i] + " = alloca i64, align 8")

    if G.procedure_optimization_level <= 1:
        for i in range(NUM_REGS, len(registers_base_list)):
            if registers_base_list[i] in c.used_reg_list:
                add_output_line("%" + registers_base_list[i] + " = alloca i32, align 4")




def _emit(c):
    """Pass 3: emit LLVM IR for every instruction."""
    for linenum in range(len(c.curproc.lines)):
        c.linenum = linenum
        G.current_line = c.curproc.lines[c.linenum]
        c.current_line = G.current_line
        c.word = c.current_line.word

        add_output_line(";" + c.current_line.orig)
        if not c.proc_instr_info[c.linenum].is_valid:
            continue

        c.paramvals = c.proc_instr_info[c.linenum].params
        c.last_reg_write = c.proc_instr_info[c.linenum].last_reg_write
        c.last_float_reg_write = c.proc_instr_info[c.linenum].last_float_reg_write
        c.current_float_stack_top = c.proc_instr_info[c.linenum].float_stack_top
        handler = _find_handler(_EMIT_TABLE, _EMIT_CACHE, c.word, _unhandled)
        if handler(c) is False:
            return False


    if not G.no_tail_calls:
        add_output_line("ret void")
    return True



def process_proc_body(proc_name):
    G.current_proc = proc_name
    c = _ProcCtx()
    c.curproc = G.proc_list[proc_name]
    c.is_inside_if = False
    c.is_static_if = False
    c.static_if_result = False
    c.if_contains_call = False
    c.current_float_stack_top = 0
    c.if_float_stack_top = 0
    c.endif_label = ""
    c.if_reg_state = [EMPTY] * NUM_REGS
    c.if_float_reg_state = [EMPTY] * NUM_FLOAT_REGS
    c.if_reg64_state = [EMPTY] * NUM_REGS64
    c.used_reg_list = {}
    c.used_float_reg_list = {}
    c.used_reg64_list = {}
    if not _analyze(c):
        return False
    _backward_pass(c)
    _declare_registers(c)
    return _emit(c)
