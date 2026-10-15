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

"""The command line and the main program."""

import os

from .generate import generate
from .reader import read_input
from .state import G, reset_state, stderr_write
from .strutil import str_split_strip


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


def _parse_options(args):
    """Set the options in G. Returns an exit status, or None."""
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

    return None



def main(args):
    result = _parse_options(args)
    if result is not None:
        return result

    result = read_input()
    if result is not None:
        return result

    return generate()
