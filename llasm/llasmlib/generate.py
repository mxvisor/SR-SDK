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

"""Second pass: writing the LLVM IR."""

import sys

from .emit import add_output_line, get_addr_to_int32, get_int32_to_ptr, get_load_type, get_new_temporary_register
from .expr import _s32
from .proc import process_proc_body
from .state import G
from .tables import EMPTY, NUM_REGS64, _llref


def write_output(output_filename):
    data = "".join(line + "\n" for line in G.output_lines).encode("latin-1")

    if output_filename == "":
        sys.stdout.flush()
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
    else:
        with open(output_filename, "wb") as fd:
            fd.write(data)


def _write_header_and_data():
    """The target layout, the CPU type and the data segments."""

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
    return None



def _write_declarations():
    """External variables, functions and procedures."""

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
    return None



def _write_intrinsics():
    """Declarations of the intrinsics that are used."""

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
    return None



def _write_procedures():
    """The procedures."""

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
    return None



def _write_ctor():
    """The pointer initialization and the constructor."""

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
    return None



def generate():
    """Write the output file. Returns an exit status."""
    if G.output_preprocessed_file:
        write_output(G.output_filename)
        return 0

    for section in (_write_header_and_data, _write_declarations, _write_intrinsics, _write_procedures, _write_ctor):
        result = section()
        if result is not None:
            return result

    write_output(G.output_filename)
    return 0
