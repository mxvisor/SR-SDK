#
#  Copyright (C) 2019-2026 Roman Pauer (SConscript)
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

import os
import re
import shutil
import sys

# The SDK's default for a game that has no game/llasm/SConscript of its own:
#   game_llasm_dir   the game's game/llasm/ in the build tree
#   sdk_llasm_dirs   the llasm/ directories of the runtime (runtime/<platform>/llasm,
#                    and runtime/win32/winapi/llasm); a file the game has under the
#                    same name replaces the runtime's
#   obj_runtime      where the objects of the runtime's .c go
#   ir_dir           build/obj/ir: the bitcode of every .llasm, one for all devices
Import('device', 'env', 'gen_dir', 'ir_dir', 'llasm_tool', 'llasm_deps', 'gen_src_dir', 'gen_main', 'gen_deps', 'dll_masters', 'llasm_inc_dirs',
       'game_llasm_dir', 'sdk_llasm_dirs', 'sdk_root', 'obj_runtime')

llasm_include_re = re.compile(r'^include\s+(\S+)\r?$', re.M)

# llasm opens an include beside the file it was given first, then along -I.
# The master of stage 3 is in build/gen/<module>/<arch>/ while the includes
# it shares with the hand-written .llasm (llasm.llinc, extern.llinc,
# macros.llinc) stay in game/llasm/, so a scan has to know both -- otherwise
# SCons either loses the dependency or demands a file nobody builds.
include_dirs = [gen_src_dir] + [dll[1] for dll in dll_masters] + llasm_inc_dirs

def llasmfile_scan(node, env, path):
    contents = node.get_text_contents()
    found = []
    for name in llasm_include_re.findall(contents):
        hit = None
        for directory in [node.dir.get_abspath()] + include_dirs:
            candidate = os.path.join(directory, name)
            if os.path.exists(candidate):
                hit = File(candidate)
                break
        found.append(hit if hit is not None else name)
    return found

llasmscan = Scanner(function = llasmfile_scan,
                    skeys = ['.llasm'],
                    recursive = True)

SourceFileScanner.add_scanner('.llasm', llasmscan)

# the llasm command (SR-SDK/llasm/llasm.py or llasm=<path>), from SR-SDK/scons/game.py
# Two steps: llasm and opt make a bitcode that does not depend on the device
# (the same LLASMFLAGS everywhere, no triple for opt), in build/obj/ir/, shared
# by the devices; only llc runs per device. A build of a second device finds the
# bitcode in SCons's database (build/.sconsign, one for all devices) and skips it.
# pipefail: a failing llasm must stop the build and leave no bitcode behind
# (the $rc is escaped for SCons, which would take it for one of its own variables)
bld_ir = Builder(action = "bash -o pipefail -c '" + llasm_tool + " $SOURCE -O $LLASMFLAGS | opt -O3 -o $TARGET || { rc=$?; rm -f $TARGET; exit $$rc; }'",
                 suffix = '.bc',
                 src_suffix = '.llasm'
                )
bld = Builder(action = 'llc -O=3 -filetype=obj $LLVMFLAGS $SOURCE -o $TARGET',
              suffix = "$OBJSUFFIX",
              src_suffix = '.bc'
             )
env.Append(BUILDERS = {'llasm_ir' : bld_ir, 'llasm' : bld})

env_path = env['ENV']['PATH'].split(os.pathsep)
if sys.version_info >= (3, 3):
    for util in ['llasm', 'opt', 'llc']:
        util_path = shutil.which(util)
        if util_path is not None:
            util_dir = os.path.dirname(util_path)
            if util_dir not in env_path:
                env_path.append(util_dir)
else:
    for path_dir in os.environ['PATH'].split(os.pathsep):
        if path_dir not in env_path:
            env_path.append(path_dir)
env['ENV']['PATH'] = os.pathsep.join(env_path)
# LLASM_SYMBOLS=1: the procedures keep their names in the object (nm, gdb)
if os.environ.get('LLASM_SYMBOLS'):
    env['ENV']['LLASM_SYMBOLS'] = os.environ['LLASM_SYMBOLS']
if env['CC'] == 'cl':
    env.Append(LLASMFLAGS = ' -func-name strncasecmp=_strnicmp')
# -I has to be absolute: llasm resolves a relative -I against the directory of
# the file it was given, not against the working directory.
env.Append(LLASMFLAGS = ''.join(' -I ' + directory for directory in llasm_inc_dirs))

game_files = set(os.listdir(Dir(game_llasm_dir).srcnode().abspath)) \
             if os.path.isdir(Dir(game_llasm_dir).srcnode().abspath) else set()

obj = env.Object(Glob(os.path.join(game_llasm_dir, '*.c')))
llasm_files = [str(node) for node in Glob(os.path.join(game_llasm_dir, '*.llasm'))]

for sdk_dir in sdk_llasm_dirs:
    variant = os.path.join(obj_runtime, os.path.relpath(sdk_dir, os.path.join(sdk_root, 'runtime')))
    VariantDir(variant, sdk_dir, duplicate = 0)
    for name in sorted(os.listdir(sdk_dir)):
        if name in game_files:
            continue
        if name.endswith('.c'):
            obj += env.Object(os.path.join(variant, name))
        elif name.endswith('.llasm'):
            llasm_files.append(os.path.join(variant, name))

# The master of stage 3 is in build/gen/<module>/<arch>/, not here: its
# object goes to build/obj/<device>/gen/, its source stays where the
# pipeline put it. Sorted by name, so the link order does not depend on
# where a file comes from.
if gen_main:
    llasm_files.append(str(gen_main))
# the masters of the DLLs recompiled as modules (dll_modules= of game.cfg)
dll_deps = {str(master): deps for master, _, deps in dll_masters}
llasm_files += list(dll_deps)
llasm_files.sort(key = os.path.basename)

for llasm_file in llasm_files:
    name = os.path.splitext(os.path.basename(llasm_file))[0]
    target = File(os.path.join(gen_dir, name + env['OBJSUFFIX']))
    llasm_ir = env.llasm_ir(target = File(os.path.join(ir_dir, name + '.bc')), source = File(llasm_file))
    # the driver is named in the action, so SCons has to be told about it
    env.Depends(llasm_ir, llasm_deps)
    if gen_main and llasm_file == str(gen_main):
        env.Depends(llasm_ir, gen_deps)
    if llasm_file in dll_deps:
        env.Depends(llasm_ir, dll_deps[llasm_file])
    obj += env.llasm(target = target, source = llasm_ir)

Return('obj')
