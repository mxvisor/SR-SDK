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

"""Constants and tables."""

import re


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


_HEXDIGITS = "0123456789abcdef"


_DEC_RE = re.compile(r"[+-]?[0-9]+\Z")
