#
#  Copyright (C) 2016-2026 Roman Pauer (SConstruct)
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

# The whole build of one game, as one SCons graph:
#
#     stage 1  host    build/host/<host>/sr-<arch>/  SR (or SRW)
#     stage 0  input   build/input/<module>/         the original after sha256
#     stage 2  recomp  build/recomp/<module>/<arch>/ SR.exe's (SRW.exe's) working directory
#     stage 3  gen     build/gen/<module>/<arch>/    compact_source's output
#     stage 4  obj     build/obj/<device>/{runtime,game,support,gen}
#     stage 5  bin     build/bin/<device>/<program>
#     stage 6  dist    build/dist/<device>/
#
# Everything a game is, it says in game/game.cfg; this file never names it.
# All paths are absolute: it is read from the SDK's scons/, not from the root.
# The SDK is a submodule of the game (SR-SDK/), SR a submodule of the SDK (SR/).
#
#     scons stage=host   stops after the host tools
#     scons stage=obj    stops before linking
#     BUILD_DIR=...      puts build/ somewhere else

import glob
import hashlib
import os
import platform
import re
import shutil
import sys

# ------------------------------------------------------------- game/game.cfg --

def read_cfg(path):
    cfg = {}
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            key, value = line.split('=', 1)
            cfg[key.strip()] = value.strip()
    return cfg


def _keys(cfg, name):
    return [value.strip() for value in cfg[name].split(',') if value.strip()]


ROOT = Dir('#').abspath                         # the game
SDK_ROOT = os.path.dirname(Dir('.').srcnode().abspath)
SR_ROOT = os.path.join(SDK_ROOT, 'SR')
CFG = read_cfg(os.path.join(ROOT, 'game', 'game.cfg'))

ARCHS = _keys(CFG, 'archs')
MODULES = _keys(CFG, 'modules')
VARIANTS = _keys(CFG, 'variants')
DEVICES = _keys(CFG, 'devices')
PROGRAM = CFG.get('program', '-'.join(MODULES).lower())

if len(MODULES) != 1:
    raise NotImplementedError(
        'game.cfg: %d modules, but build/obj/<device>/gen/ has no per-module '
        'subdirectory yet' % len(MODULES))
if len(ARCHS) != 1:
    raise NotImplementedError('game.cfg: one arch per game for now, got %s' % ARCHS)

MODULE = MODULES[0]
ARCH = ARCHS[0]
VARIANT = VARIANTS[0]

# The original's platform picks the runtime layer and the recompiler: an LE
# executable of DOS is SR's (SR/SR, SR.exe), a PE executable of Windows is
# SRW's (SR/SRW, SRW.exe) -- each with its own SR_defs.h and its own .sci files.
PLATFORM = CFG.get('platform', 'dos')
if PLATFORM not in ('dos', 'win32'):
    raise NotImplementedError('game.cfg: platform=%r: known platforms are dos and win32'
                              % PLATFORM)
SR_SOURCE_DIR = os.path.join(SR_ROOT, 'SRW' if PLATFORM == 'win32' else 'SR')
SR_EXE_NAME = 'SRW.exe' if PLATFORM == 'win32' else 'SR.exe'

# The stages in order; "done" is what this build can actually stop at.
STAGES = ['host', 'input', 'recomp', 'gen', 'obj', 'bin', 'dist']
DONE = ['host', 'input', 'recomp', 'gen', 'obj', 'bin']

stage = ARGUMENTS.get('stage', 'bin')
if stage not in STAGES:
    raise ValueError('stage=%s: known stages are %s' % (stage, ', '.join(STAGES)))
if stage not in DONE:
    raise NotImplementedError('stage=%s is not built yet (done: %s)'
                              % (stage, ', '.join(DONE)))
up_to = STAGES.index(stage)


def want(name):
    return STAGES.index(name) <= up_to


# ------------------------------------------------------------------- keys --

def host_key():
    machine = platform.machine().lower()
    machine = {'x86_64': 'x64', 'amd64': 'x64',
               'i386': 'x86', 'i486': 'x86', 'i586': 'x86', 'i686': 'x86'}.get(machine, machine)
    system = platform.system()
    if system == 'Linux':
        return 'linux-' + machine
    if system in ('Windows', 'Microsoft'):
        return 'windows-' + machine
    return system.lower() + '-' + machine


HOST = host_key()

# BUILD_DIR over the default build/ keeps a cacheable tree off the source disk.
BUILD = ARGUMENTS.get('BUILD_DIR', os.environ.get('BUILD_DIR', os.path.join(ROOT, 'build')))
if not os.path.isabs(BUILD):
    BUILD = os.path.normpath(os.path.join(ROOT, BUILD))

HOST_SR = os.path.join(BUILD, 'host', HOST, 'sr-' + ARCH)

# Where the recompilation input of each stage lives, the directories of the
# layout in "Каталоги сборки": the game's own inputs are read from game/, the
# results of running SR and compact_source are written to build/ and stay
# there, so two SDK versions can be compared with a plain diff -r.
RECOMP_SRC = os.path.join(ROOT, 'game', 'recomp', MODULE, VARIANT)
INPUT_DIR = os.path.join(BUILD, 'input', MODULE)
INPUT_EXE = os.path.join(INPUT_DIR, MODULE + '.EXE')
RECOMP_DIR = os.path.join(BUILD, 'recomp', MODULE, ARCH)
GEN_SRC_DIR = os.path.join(BUILD, 'gen', MODULE, ARCH)
LLASM_SRC_DIR = os.path.join(ROOT, 'game', 'llasm')

# The shared code: the SDK's runtime (core, then the original's platform from
# game.cfg) and SR's own llasm-support, used in place rather than copied.
RUNTIME_CORE = os.path.join(SDK_ROOT, 'runtime', 'core')
RUNTIME_DIRS = [RUNTIME_CORE, os.path.join(SDK_ROOT, 'runtime', PLATFORM)]
# runtime/win32/winapi/ is the Win32 API layer (kernel32, user32, DirectDraw...)
# of a Windows original that runs on it; a game that does not (winapi=no in
# game.cfg) gets only what is directly in runtime/win32/.
WINAPI = PLATFORM == 'win32' and CFG.get('winapi', 'yes') != 'no'
if WINAPI:
    RUNTIME_DIRS.append(os.path.join(RUNTIME_DIRS[1], 'winapi'))
LLASM_SUPPORT = os.path.join(SR_ROOT, 'SR', 'llasm-support')
LLASM_INC_DIRS = [LLASM_SRC_DIR,
                  os.path.join(RUNTIME_DIRS[1], 'llasm'),
                  LLASM_SUPPORT]

# ------------------------------------------------------------------ env --

vars = Variables('custom.py')
vars.Add(BoolVariable('debug', 'Build with debug information', False))
vars.Add(EnumVariable('device', 'Set target device', '',
                      allowed_values=('', ) + tuple(DEVICES),
                      ignorecase=2))
vars.Add('llasm', 'The llasm of stage 4: "py" for the SDK\'s llasm/llasm.py, '
                  'or the path of an llasm executable (the D one of SR/llasm/)', 'py')
vars.Add('toolprefix', 'Prefix of the MinGW-w64 tools for pc64-windows (cross builds)',
         'x86_64-w64-mingw32-')
env = Environment(variables=vars)

device = env['device']
if device == '':
    if env['PLATFORM'] == 'win32':
        device = 'pc64-windows'
    else:
        machine = platform.machine().lower()
        device = 'arm64-linux' if machine in ('aarch64', 'arm64') else 'pc64-linux'
    if device not in DEVICES:
        device = DEVICES[0]
    env['device'] = device

Help(vars.GenerateHelpText(env))
Help("""
Environment variables:
  - PATH_INC: paths to find include files
  - PATH_LIB: paths to find library files
  - BUILD_DIR: where build/ lives (default <root>/build)
  - ANDROID_NDK_HOME: NDK path, required for device=arm64-android

Stages: host -> input -> recomp -> gen -> obj -> bin -> dist;
scons stage=<name> stops at that one.
""")

if device not in ('pc64-linux', 'pc64-windows', 'arm64-linux', 'arm64-android'):
    raise NotImplementedError('device %r: this build module knows pc64-linux, '
                              'pc64-windows, arm64-linux and arm64-android' % device)

if device == 'pc64-windows':
    if env['PLATFORM'] == 'win32':
        env = Environment(variables=vars, tools=['mingw'])
        env['device'] = device
    else:
        prefix = env['toolprefix']
        for tool, name in (('CC', 'gcc'), ('CXX', 'g++'), ('LINK', 'gcc'),
                           ('AR', 'ar'), ('RANLIB', 'ranlib')):
            env[tool] = prefix + name
        env['PROGSUFFIX'] = '.exe'
        # Fedora's cross sysroot; elsewhere give PATH_INC/PATH_LIB
        sysroot = '/usr/' + prefix.rstrip('-') + '/sys-root/mingw'
        if os.path.isdir(sysroot):
            env.Append(CPPPATH=[sysroot + '/include/SDL2'], LIBPATH=[sysroot + '/lib'])

if device == 'arm64-android':
    # The NDK clang is picked from ANDROID_NDK_HOME; headers and .so files come
    # from the sysroot this project installs SDL into.  No -m64 anywhere: the
    # aarch64 compiler does not know it, and llasm/llc get the width from their
    # own flags.
    ndk = os.environ.get('ANDROID_NDK_HOME', '')
    if not ndk:
        raise Exception('device arm64-android: set ANDROID_NDK_HOME to '
                        '<sdk>/ndk/<version>')
    llvm_bin = os.path.join(ndk, 'toolchains', 'llvm', 'prebuilt',
                            'linux-x86_64', 'bin')
    clang = os.path.join(llvm_bin, 'aarch64-linux-android21-clang')
    if not os.path.exists(clang):
        raise Exception('device arm64-android: %s not found' % clang)
    env['CC'] = clang
    env['CXX'] = clang + '++'
    env['LINK'] = clang + '++'
    env['AR'] = os.path.join(llvm_bin, 'llvm-ar')
    env['RANLIB'] = os.path.join(llvm_bin, 'llvm-ranlib')
    android_sysroot = os.path.join(BUILD, 'sysroot-android-arm64', 'usr')
    env.Append(CPPPATH=[os.path.join(android_sysroot, 'include', 'SDL2')],
               LIBPATH=[os.path.join(android_sysroot, 'lib')])
    # All objects are compiled with -fPIC (or llc -relocation-model=pic), so
    # the Object() results of the SConscripts are fine as shared library
    # sources -- SCons otherwise insists they were made by SharedObject().
    env['STATIC_AND_SHARED_OBJECTS_ARE_THE_SAME'] = 1

if 'PATH_INC' in os.environ:
    env.Append(CPPPATH=os.environ['PATH_INC'].split(os.pathsep))
if 'PATH_LIB' in os.environ:
    env.Append(LIBPATH=os.environ['PATH_LIB'].split(os.pathsep))

env.Append(CCFLAGS='-O2 -Wall -Wextra -x c++')
if env['debug']:
    env.Append(CCFLAGS='-g')

# Every build is position-independent: guest pointers are 32-bit offsets from
# the load-time pointer_offset (Game_memory.c initialize_pointer_offset, macro
# PTROFS_64BIT), never absolute host addresses, so the program may be loaded
# anywhere (ASLR). There is no fixed-address mode.
if device == 'pc64-linux':
    # a PIE; the recompiled code is llasm -pic -ptrofs
    env.Append(CCFLAGS='-m64 -DPTROFS_64BIT -fpie',
               CPPPATH=['/usr/include/SDL2', '/usr/local/include/SDL2'],
               LINKFLAGS='-m64 -pie -Wl,-z,noexecstack',
               LIBS=['SDL2_mixer', 'SDL2', 'pthread', 'dl', 'm'],
               LLASMFLAGS='-m64 -inline-idiv -pic -ptrofs',
               LLVMFLAGS='-mtriple=x86_64-unknown-linux-gnu --relocation-model=pic')
elif device == 'arm64-linux':
    # native aarch64 Linux: a PIE, as pc64-linux.  No -m64 in CCFLAGS or
    # LINKFLAGS -- the aarch64 compiler does not know it; the llasm -m64 is
    # llasm's own flag (64-bit pointers) and stays, and llc gets the width
    # from its triple.
    env.Append(CCFLAGS='-DPTROFS_64BIT -fpie',
               CPPPATH=['/usr/include/SDL2', '/usr/local/include/SDL2'],
               LINKFLAGS='-pie -Wl,-z,noexecstack',
               LIBS=['SDL2_mixer', 'SDL2', 'pthread', 'dl', 'm'],
               LLASMFLAGS='-m64 -inline-idiv -pic -ptrofs',
               LLVMFLAGS='-mtriple=aarch64-unknown-linux-gnu --relocation-model=pic')
elif device == 'arm64-android':
    # the NDK linker loads the .so anywhere and forbids text relocations
    env.Append(CCFLAGS='-fPIC -DPTROFS_64BIT',
               LIBS=['SDL2_mixer', 'SDL2', 'android', 'log'],
               LLASMFLAGS='-m64 -inline-idiv -pic -ptrofs',
               LLVMFLAGS='-mtriple=aarch64-linux-android21 -relocation-model=pic')
else:
    # a GUI program, as the other games: without a console main.c sends
    # stdout and stderr to a log in the write root, and shows its end
    # when the game stops on an error (the CD check's among them).
    # ASLR: dynamicbase + a .reloc section; high-entropy-va needs a 64-bit image.
    env.Append(CCFLAGS='-m64 -DPTROFS_64BIT -Dmain=SDL_main',
               LINKFLAGS='-m64 -mwindows -Wl,--dynamicbase,--enable-reloc-section,'
                         '--high-entropy-va,--nxcompat,--tsaware',
               LIBS=['mingw32', 'SDL2main', 'SDL2_mixer', 'SDL2', 'winmm'],
               LLASMFLAGS='-m64 -inline-idiv -ptrofs',
               LLVMFLAGS='-mtriple=x86_64-pc-windows-gnu')

# extra libraries of the game for a device: libs.<device>=GL,... in game.cfg
env.Append(LIBS=[lib.strip() for lib in CFG.get('libs.' + device, '').split(',')
                 if lib.strip()])

for utility in ['opt', 'llc']:
    utility_path = shutil.which(utility)
    if utility_path is not None:
        utility_directory = os.path.dirname(utility_path)
        if utility_directory not in env['ENV']['PATH'].split(os.pathsep):
            env['ENV']['PATH'] += os.pathsep + utility_directory

# ------------------------------------------------- stage 1: host tools --

# The SDK and its SR/ are submodules and stay read-only: one checkout of the
# SDK is meant to serve several games at once, and nothing of theirs may land
# in it. So SR is
# built from a copy under build/host/<host>/sr-<arch>/, and that copy is where
# OUTPUT_TYPE gets set -- the one line SR_defs.h hard-codes its backend
# through, with no way to pass it on the command line. The copy is of SR/SR
# for platform=dos and of SR/SRW for platform=win32.
SR_OUTPUT_TYPES = {'x86': 'OUT_X86', 'arm': 'OUT_ARM_LINUX',
                   'x64': 'OUT_X64', 'llasm': 'OUT_LLASM'}


def sync_tree(source, target, skip):
    """Copy source into target, keeping the mtimes of what did not change so
    SCons does not see a tree that is new on every run. Object files and the
    named build outputs are never copied: they belong to the copy, not to the
    SDK."""
    if not os.path.isdir(target):
        os.makedirs(target)
    for name in os.listdir(source):
        if name in skip or name.endswith('.o'):
            continue
        src = os.path.join(source, name)
        dst = os.path.join(target, name)
        if os.path.isdir(src) and not os.path.islink(src):
            sync_tree(src, dst, skip)
        elif not os.path.exists(dst) or os.path.getmtime(src) > os.path.getmtime(dst) + 1:
            shutil.copy2(src, dst)


def set_output_type(path, arch):
    want_define = SR_OUTPUT_TYPES[arch]
    with open(path, 'rt') as stream:
        text = stream.read()
    new_text, count = re.subn(r'^#define\s+OUTPUT_TYPE\s+\S+.*$',
                              '#define OUTPUT_TYPE  ' + want_define, text, flags=re.M)
    if count != 1:
        raise RuntimeError('%s: expected exactly one #define OUTPUT_TYPE, found %d'
                           % (path, count))
    if new_text != text:
        with open(path, 'wt') as stream:
            stream.write(new_text)


# SCons builds default targets only when they live under the project
# directory, and one Default() call replaces the implicit set entirely --
# so every stage names its targets, and BUILD_DIR can point outside the
# tree (the CI cache of the plan) without the build silently doing nothing.
defaults = []

if want('host'):
    sync_tree(SR_SOURCE_DIR, HOST_SR,
              skip={'SR.exe', 'SR.exe.llasm', 'SR.exe.x86', 'SR.exe.x64',
                    'SR.exe.arm', 'SRW.exe', '.sconsign.dblite', 'custom.py'})
    set_output_type(os.path.join(HOST_SR, 'SR_defs.h'), ARCH)
    SConscript(os.path.join(HOST_SR, 'SConstruct'))
    defaults.append(os.path.join(HOST_SR, SR_EXE_NAME))

# The llasm of stage 4: the SDK's Python port of SR/llasm/llasm.d, which gives
# the same output (llasm/README.md), run by the Python that runs SCons -- so
# a game needs no D compiler. llasm=<path> takes an executable instead.
if env['llasm'] == 'py':
    LLASM_PY = os.path.join(SDK_ROOT, 'llasm', 'llasm.py')
    llasm_tool = '"%s" "%s"' % (sys.executable, LLASM_PY)
    # the script is only the entry point: the translator is the package beside it
    llasm_deps = [LLASM_PY] + sorted(glob.glob(os.path.join(SDK_ROOT, 'llasm', 'llasmlib', '*.py')))
else:
    llasm_path = os.path.abspath(env['llasm'])
    llasm_tool = '"%s"' % llasm_path
    llasm_deps = [llasm_path]

# ------------------------------------------- stages 0, 2, 3: input -> gen --

def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def copy_if_newer(source, target):
    """copy2, so the target keeps the source's mtime and an unchanged input
    does not look new to SCons on the next run."""
    if os.path.exists(target) and os.path.getmtime(source) <= os.path.getmtime(target) + 1:
        return
    directory = os.path.dirname(target)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    shutil.copy2(source, target)


def shell_quote(path):
    return "'" + path.replace("'", "'\\''") + "'"


# Everything SR writes beside <module>.asm, from
# game/recomp/<module>/<variant>/outputs: the segments of this EXE, and the
# list is game data, not something this file may guess at.
SR_EXTRAS = []
_compact = os.path.join(RECOMP_SRC, 'compact')
_outputs = os.path.join(RECOMP_SRC, 'outputs')
if os.path.exists(_compact):
    with open(_compact, 'rt') as stream:
        SR_EXTRAS = [line.strip() for line in stream
                     if line.strip() and not line.lstrip().startswith('#')]
elif os.path.exists(_outputs):
    with open(_outputs, 'rt') as stream:
        SR_EXTRAS = [line.strip() for line in stream
                     if line.strip() and not line.lstrip().startswith('#')]

GEN_MAIN = os.path.join(GEN_SRC_DIR, MODULE + '.llasm')
RECOMP_ASM = os.path.join(RECOMP_DIR, MODULE + '.asm')

if want('input'):
    # Stage 0: the original, checked against the hash the variant records.
    # The check runs here rather than in build.sh so that it cannot be
    # skipped: nothing downstream ever reads an EXE that is not the recorded one.
    with open(os.path.join(RECOMP_SRC, 'sha256'), 'rt') as stream:
        expected = stream.read().split()[0].lower()

    if not (os.path.exists(INPUT_EXE) and sha256_of(INPUT_EXE) == expected):
        source = ARGUMENTS.get('input_exe', CFG.get('input_exe', ''))
        if source and not os.path.isabs(source):
            source = os.path.join(ROOT, source)
        if not source or not os.path.exists(source):
            raise Exception(
                'stage 0: %s is not there and no original to take it from '
                '(scons input_exe=/path/to/the/original, or game.cfg input_exe=%s)'
                % (INPUT_EXE, CFG.get('input_exe', '')))
        actual = sha256_of(source)
        if actual != expected:
            raise Exception('stage 0: sha256 of %s is %s, expected %s (game/recomp/%s/%s/sha256)'
                            % (source, actual, expected, MODULE, VARIANT))
        if not os.path.isdir(INPUT_DIR):
            os.makedirs(INPUT_DIR)
        if os.path.exists(INPUT_EXE):
            os.remove(INPUT_EXE)
        shutil.copy2(source, INPUT_EXE)

if want('recomp'):
    # Stage 2: SR's working directory. SR.cfg, bssborder.csv and the .sci
    # files are read from the current directory, so the .sci files of
    # game/recomp/.../llasm/ land beside them here, the way build-llasm.sh
    # copies them in.
    for name in ('SR.cfg', 'bssborder.csv'):
        copy_if_newer(os.path.join(RECOMP_SRC, name), os.path.join(RECOMP_DIR, name))
    for sci in sorted(glob.glob(os.path.join(RECOMP_SRC, 'llasm', '*.sci'))):
        copy_if_newer(sci, os.path.join(RECOMP_DIR, os.path.basename(sci)))

    if not os.path.isdir(RECOMP_DIR):
        os.makedirs(RECOMP_DIR)

    sr_exe = os.path.join(HOST_SR, SR_EXE_NAME)
    sr_logs = [os.path.join(RECOMP_DIR, 'sr.out'), os.path.join(RECOMP_DIR, 'sr.err')]
    sr_targets = [RECOMP_ASM] + [os.path.join(RECOMP_DIR, name) for name in SR_EXTRAS] + sr_logs
    sr_sources = [INPUT_EXE, File(sr_exe),
                  File(os.path.join(RECOMP_SRC, 'SR.cfg')),
                  File(os.path.join(RECOMP_SRC, 'bssborder.csv'))]
    sr_sources += [File(path) for path in sorted(
        glob.glob(os.path.join(RECOMP_SRC, 'llasm', '*.sci')))]
    if os.path.exists(_outputs):
        sr_sources.append(File(_outputs))

    # SRW reads relocations.csv from its working directory, but only for a PE
    # whose relocations were stripped (SRW_loader.c); a PE with its .reloc
    # section never needs it, so the file is optional.
    _relocs = os.path.join(RECOMP_SRC, 'relocations.csv')
    if os.path.exists(_relocs):
        copy_if_newer(_relocs, os.path.join(RECOMP_DIR, 'relocations.csv'))
        sr_sources.append(File(_relocs))

    defaults += env.Command(sr_targets, sr_sources,
                            'cd %s && %s %s %s > sr.out 2> sr.err'
                            % (shell_quote(RECOMP_DIR), shell_quote(sr_exe),
                               shell_quote(INPUT_EXE), shell_quote(MODULE + '.asm')))

if want('gen'):
    # Stage 3: compact_source renames <module>.asm and compacts the data
    # segments, and does it in place -- so the raw SR output is copied into
    # build/gen/... first, and build/recomp/... keeps what SR wrote for
    # diffing against another SDK.
    compact = os.path.join(SDK_ROOT, 'tools', 'compact_source.py')
    if not os.path.isdir(GEN_SRC_DIR):
        os.makedirs(GEN_SRC_DIR)

    gen_targets = [GEN_MAIN] + [os.path.join(GEN_SRC_DIR, name) for name in SR_EXTRAS]
    gen_sources = [RECOMP_ASM] + [os.path.join(RECOMP_DIR, name) for name in SR_EXTRAS] \
        + [File(compact)]

    gen_cmd = 'mkdir -p %s && cd %s && cp %s %s . && python3 compact_source.py --dialect llasm --module %s' % (
        shell_quote(GEN_SRC_DIR), shell_quote(GEN_SRC_DIR),
        shell_quote(compact),
        ' '.join(shell_quote(path) for path in [RECOMP_ASM]
                 + [os.path.join(RECOMP_DIR, name) for name in SR_EXTRAS]),
        shell_quote(MODULE))
    defaults += env.Command(gen_targets, gen_sources, gen_cmd)

# ------------------------------------------------- stage 4: obj, stage 5: bin --

if want('obj'):
    obj_device = os.path.join(BUILD, 'obj', device)
    obj_game = os.path.join(obj_device, 'game')
    gen_dir = os.path.join(obj_device, 'gen')
    bin_device = os.path.join(BUILD, 'bin', device)

    # what game/llasm/SConscript asks for: where stage 3 put the generated
    # master, and where the hand-written includes it pulls in live, searched
    # in this order -- the game's own first, so a same-named file there wins
    gen_src_dir = GEN_SRC_DIR
    gen_main = GEN_MAIN
    llasm_inc_dirs = LLASM_INC_DIRS

    # The game's sources, then the SDK's runtime, then SR's shared code. A .c
    # of the runtime that the game has under the same relative path is not
    # built (an override). Headers are looked up the same way for the game's
    # .c only: a .c of the runtime finds the runtime's headers beside it first.
    env.Append(CPPPATH=[os.path.join(ROOT, 'game')] + RUNTIME_DIRS
               + [os.path.join(RUNTIME_CORE, 'display'),
                  os.path.join(SR_ROOT, 'scaler-plugins'), LLASM_SUPPORT])

    VariantDir(obj_game, os.path.join(ROOT, 'game'), duplicate=0)

    def game_dir(sub):
        # the game's SConscript of that directory if it has one, else all its .c
        if os.path.exists(os.path.join(ROOT, 'game', sub, 'SConscript')):
            return SConscript(os.path.join(obj_game, sub, 'SConscript'),
                              exports='device env')
        return env.Object(Glob(os.path.join(obj_game, sub, '*.c')))

    runtime_objs = []
    for runtime_dir in RUNTIME_DIRS:
        obj_runtime = os.path.join(obj_device, 'runtime',
                                   os.path.relpath(runtime_dir, os.path.join(SDK_ROOT, 'runtime')))
        VariantDir(obj_runtime, runtime_dir, duplicate=0)
        for sub in ('', 'display', 'audio', 'input'):
            for source in sorted(glob.glob(os.path.join(runtime_dir, sub, '*.c'))):
                name = os.path.join(sub, os.path.basename(source))
                if not os.path.exists(os.path.join(ROOT, 'game', name)):
                    runtime_objs += env.Object(os.path.join(obj_runtime, name))

    # llasm-support: the C half of every llasm_X.llinc the game's llasm.llinc
    # (or SR's, if the game has none) includes, unless game/llasm/ has its own
    support_objs = []
    if ARCH == 'llasm':
        obj_support = os.path.join(obj_device, 'support')
        VariantDir(obj_support, LLASM_SUPPORT, duplicate=0)
        llinc = os.path.join(LLASM_SRC_DIR, 'llasm.llinc')
        if not os.path.exists(llinc):
            llinc = os.path.join(LLASM_SUPPORT, 'llasm.llinc')
        with open(llinc, 'rt') as stream:
            active = re.findall(r'^include\s+(llasm_\w+)\.llinc\s*$', stream.read(), re.M)
        for name in sorted(active):
            if (os.path.exists(os.path.join(LLASM_SUPPORT, name + '.c'))
                    and not os.path.exists(os.path.join(LLASM_SRC_DIR, name + '.c'))):
                support_objs += env.Object(os.path.join(obj_support, name + '.c'))

    game_objs = runtime_objs + game_dir('')
    game_objs += game_dir('display')
    game_objs += game_dir('audio')
    game_objs += game_dir('input')
    game_objs += support_objs
    game_objs += SConscript(os.path.join(obj_game, 'llasm', 'SConscript'),
                            exports='device env gen_dir llasm_tool llasm_deps '
                                    'gen_src_dir gen_main llasm_inc_dirs')
    defaults += game_objs

if want('bin'):
    # The link order: the runtime's .c, the game's own .c, the three SDL
    # frontends, llasm-support, the game's llasm .c glue, then the .llasm
    # output of stage 3.
    if device == 'arm64-android':
        # SDLActivity loads libmain.so by name, so the shared object is
        # called main, not the program name from game.cfg.
        defaults += env.SharedLibrary(os.path.join(BUILD, 'bin', device, 'main'),
                                      game_objs)
    else:
        defaults += env.Program(os.path.join(BUILD, 'bin', device, PROGRAM), game_objs)

Default(defaults)
