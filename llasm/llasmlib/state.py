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

"""Data classes and the global translator state G."""

import sys

from .tables import EMPTY, NUM_FLOAT_REGS, NUM_REGS, NUM_REGS64


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
        # the address the label has in the original program (0 if unknown);
        # the name of a label of a disassembled program is its address
        self.ivalue = 0


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
    # the guest's data segments are copied into the guest's memory, so that
    # the guest reaches them at the addresses they had in the original program
    G.guest_data_in_memory = False
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
