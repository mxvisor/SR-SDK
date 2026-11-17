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
#     stage 6  dist    build/distrib/<device>/
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
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(Dir('.').srcnode().abspath), 'scons'))
import glibc_max     # noqa: E402

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

# A repository may build several programs of one game (another exe of the same
# disc): game/ is its main target, game-<name>/ the others, each with a game.cfg
# of its own; game/game.cfg lists them (targets=). `scons target=<name>` builds
# one, target=default (or none) the main one. The disc, the game directory
# (game/distrib.cfg), build/bin and build/distrib are the repository's, shared by
# all; the stages up to obj of a target go to build/target-<name>/.
TARGET = ARGUMENTS.get('target', 'default')
GAME_DIR = os.path.join(ROOT, 'game' if TARGET == 'default' else 'game-' + TARGET)
if not os.path.isfile(os.path.join(GAME_DIR, 'game.cfg')):
    raise Exception('target=%s: no %s' % (TARGET, os.path.join(GAME_DIR, 'game.cfg')))
CFG = read_cfg(os.path.join(GAME_DIR, 'game.cfg'))

# `scons clean` and `scons distclean` build nothing.  distclean removes the build
# tree (with SCons's own database) and the run directory.  clean keeps what is
# unpacked from the disc image (build/disc, its cue sheet in build/cd, the game
# directory build/distrib/data and its stamp): the rest is made from the sources
# on any machine, but the tree is also copied to one that has no disc image.
KEEP_ON_CLEAN = (('disc',), ('cd',), ('distrib', 'data'), ('distrib', 'data.stamp'), ('disc-image',))

# image=<path> builds from another disc image than image= of game/distrib.cfg (the
# same game in another language: game/recomp/<module>/<variant>/sha256 lists its
# exe too), and the choice stays in build/disc-image for the next builds and for
# tools/disc_tree.py and tools/distrib_cd.py; image=default goes back to the cfg's.
# `clean` keeps it with the unpacked disc, `distclean` forgets it.
DISC_IMAGE_CHOICE = os.path.join(ROOT, 'build', 'disc-image')
if ARGUMENTS.get('image'):
    _image = ARGUMENTS['image']
    if _image == 'default':
        if os.path.exists(DISC_IMAGE_CHOICE):
            os.remove(DISC_IMAGE_CHOICE)
    else:
        _path = _image if os.path.isabs(_image) else os.path.join(ROOT, _image)
        if not os.path.isfile(_path):
            raise Exception('image=%s: no such disc image' % _image)
        _image = os.path.relpath(os.path.abspath(_path), ROOT)
        os.makedirs(os.path.dirname(DISC_IMAGE_CHOICE), exist_ok=True)
        _old = open(DISC_IMAGE_CHOICE).read().strip() if os.path.isfile(DISC_IMAGE_CHOICE) else None
        if _old != _image:
            with open(DISC_IMAGE_CHOICE, 'wt') as stream:
                stream.write(_image + '\n')


def clean_tree(directory, keep):
    """Removes everything in directory but the paths of keep (tuples of names
    below it) and the directories on the way to them."""
    for entry in os.listdir(directory):
        path = os.path.join(directory, entry)
        inside = [k[1:] for k in keep if k[0] == entry]
        if not inside:
            if os.path.isdir(path) and not os.path.islink(path):
                shutil.rmtree(path, ignore_errors=True)
            else:
                os.remove(path)
        elif all(inside):
            clean_tree(path, inside)


if {'clean', 'distclean'} & set(COMMAND_LINE_TARGETS):
    build_dir = os.path.join(ROOT, 'build')
    if 'distclean' in COMMAND_LINE_TARGETS or not os.path.isdir(build_dir):
        shutil.rmtree(build_dir, ignore_errors=True)
        print('scons: cleaned build/ run/')
    else:
        clean_tree(build_dir, KEEP_ON_CLEAN)
        print('scons: cleaned build/ run/ (kept build/disc, build/cd, build/distrib/data: distclean removes them)')
    shutil.rmtree(os.path.join(ROOT, 'run'), ignore_errors=True)
    Exit(0)

ARCHS = _keys(CFG, 'archs')
MODULES = _keys(CFG, 'modules')
# DLLs of the original recompiled as modules of their own (say, the 3D drivers a game loads)
DLL_MODULES = _keys(CFG, 'dll_modules') if 'dll_modules' in CFG else []
# other programs of a DOS game recompiled as modules of their own, which the game
# runs with INT 21h AH=4Bh (say, its intro): their global names get the prefix
# <module>_ (SR.cfg global_prefix), the runtime finds them by name (exe_modules.h)
EXE_MODULES = _keys(CFG, 'exe_modules') if 'exe_modules' in CFG else []
VARIANTS = _keys(CFG, 'variants')
DEVICES = _keys(CFG, 'devices')
# the native device of an aarch64 Linux host (a board) is not a choice of the game:
# `make linux` builds for it whatever devices= lists
if 'arm64-linux' not in DEVICES:
    DEVICES.append('arm64-linux')
PROGRAM = CFG.get('program', '-'.join(MODULES).lower())
# runtime .c files the game does not build, by file name (runtime_skip=Game_cdrom.c,...)
RUNTIME_SKIP = set(_keys(CFG, 'runtime_skip')) if 'runtime_skip' in CFG else set()

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
# the stages host..obj of a target other than the main one (TARGET above)
STAGE_BUILD = BUILD if TARGET == 'default' else os.path.join(BUILD, 'target-' + TARGET)
# SCons's own database goes into the build tree too, not into the repository
os.makedirs(STAGE_BUILD, exist_ok=True)
SConsignFile(os.path.join(STAGE_BUILD, '.sconsign'))

HOST_SR = os.path.join(STAGE_BUILD, 'host', HOST, 'sr-' + ARCH)

# Where the recompilation input of each stage lives, the directories of the
# layout in "Каталоги сборки": the game's own inputs are read from game/, the
# results of running SR and compact_source are written to build/ and stay
# there, so two SDK versions can be compared with a plain diff -r.
LLASM_SRC_DIR = os.path.join(GAME_DIR, 'llasm')

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
# runtime/win32/glide/ is Glide 2.x (glide2x.dll), drawn in the window of the
# winapi layer's DirectDraw: for a game that says glide=yes in game.cfg.
GLIDE = WINAPI and CFG.get('glide', 'no') == 'yes'
if GLIDE:
    RUNTIME_DIRS.append(os.path.join(RUNTIME_DIRS[1], 'glide'))
# OpenGL in the SDK: runtime/core/gl/ (glad, a context on an SDL window,
# shaders, frame buffer objects, the scaled present), plus the gl/ directory
# of the platform (the winapi Direct3D backend, which also shows DirectDraw's
# picture). Every game shows its picture through it (display/pc.c,
# WinApi-ddraw.c); d3d_gl=yes adds Direct3D on it.
D3D_GL = WINAPI and CFG.get('d3d_gl', 'no') == 'yes'
GL = True
LLASM_SUPPORT = os.path.join(SR_ROOT, 'SR', 'llasm-support')
# the runtime's llasm/ directories: includes, and (for a game without its own
# game/llasm/SConscript) the .llasm and .c of its glue
SDK_LLASM_DIRS = [os.path.join(RUNTIME_DIRS[0], 'llasm'), os.path.join(RUNTIME_DIRS[1], 'llasm')]
if WINAPI:
    SDK_LLASM_DIRS.append(os.path.join(RUNTIME_DIRS[1], 'winapi', 'llasm'))
if GLIDE:
    SDK_LLASM_DIRS.append(os.path.join(RUNTIME_DIRS[1], 'glide', 'llasm'))
SDK_LLASM_DIRS = [path for path in SDK_LLASM_DIRS if os.path.isdir(path)]
LLASM_INC_DIRS = [LLASM_SRC_DIR] + SDK_LLASM_DIRS + [LLASM_SUPPORT]

# ------------------------------------------------------------------ env --

vars = Variables('custom.py')
vars.Add(BoolVariable('debug', 'Build with debug information', False))
vars.Add(BoolVariable('gles', 'OpenGL ES on a desktop device, to test the path of the Android build', False))
vars.Add(EnumVariable('device', 'Set target device', '',
                      allowed_values=('', ) + tuple(DEVICES),
                      ignorecase=2))
vars.Add('llasm', 'The llasm of stage 4: "py" for the SDK\'s llasm/llasm.py, '
                  'or the path of an llasm executable (the D one of SR/llasm/)', 'py')
vars.Add('toolprefix', 'Prefix of the MinGW-w64 tools for x64-windows (cross builds)',
         'x86_64-w64-mingw32-')
env = Environment(variables=vars)

device = env['device']
if device == '' and 'windows' in COMMAND_LINE_TARGETS:
    device = 'x64-windows'
if device == '':
    if env['PLATFORM'] == 'win32':
        device = 'x64-windows'
    else:
        machine = platform.machine().lower()
        device = 'arm64-linux' if machine in ('aarch64', 'arm64') else 'x64-linux'
    if device not in DEVICES:
        device = DEVICES[0]
    env['device'] = device

Help(vars.GenerateHelpText(env))
Help("""
Environment variables:
  - PATH_INC: paths to find include files
  - PATH_LIB: paths to find library files
  - BUILD_DIR: where build/ lives (default <root>/build)

image=<path> builds from another disc image than image= of game/distrib.cfg (another
release of the game whose exe game/recomp/<module>/<variant>/sha256 lists); it stays
the choice (build/disc-image) until image=default.
  - ANDROID_NDK_HOME: NDK path, required for device=arm64-android

Stages: host -> input -> recomp -> gen -> obj -> bin -> dist;
scons stage=<name> stops at that one.

target=<name> builds game-<name>/ (another program of the game, targets= of
game/game.cfg) instead of game/; its stages up to obj go to build/target-<name>/.

Targets (scons/release.SConscript; the game's Makefile has them as `make <name>`):
  linux          the native binary: build/bin/x64-linux/<program>-x64 (arm64-linux/<program>-arm64)
  windows        the MinGW cross build with the SDL DLLs beside the exe
  disc           build/disc, the disc image with game/distrib.cfg over it
  distrib-data  build/distrib/data, the disc's files and music (game/distrib.cfg)
  distrib        the release archives in build/distrib/ (linux and windows must be built)
  check          the native binary is there
  clean          remove build/ and run/ but the disc's unpacked files (build/disc, build/cd,
                 build/distrib/data), so the tree builds elsewhere without the image
  distclean      remove build/ and run/ whole
""")

if device not in ('x64-linux', 'x64-windows', 'arm64-linux', 'arm64-android'):
    raise NotImplementedError('device %r: this build module knows x64-linux, '
                              'x64-windows, arm64-linux and arm64-android' % device)


def binary_name(program, dev):
    """The file of a program on a device: <program>-<arch>, the arch being the device's
    first word (x64, arm64), so builds of two architectures sit side by side in a
    game's directory, and the name is never one of the game's (Windows adds .exe).  The
    settings, the log and the registry keep the program's name (GAME_PROGRAM_NAME):
    <program>.cfg is one for all of them."""
    return '%s-%s' % (program, dev.split('-')[0])

if device == 'x64-windows':
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
#
# The recompiled code is the same on every device: llasm -pic -ptrofs, and llc
# --relocation-model=pic (the latter changes nothing for a COFF target, whose
# code is position-independent anyway).  What differs below is the host
# compiler and the executable format: a PIE for ELF, a .reloc section and
# dynamicbase for PE (MinGW's gcc takes no -fpie: all its code is PIC).
LLASM_PIC = '-m64 -inline-idiv -pic -ptrofs'
LLC_PIC = '--relocation-model=pic'
# AArch64's llc runs the MachineOutliner by default: it indexes the instructions
# of the whole module and finds nothing to outline in this code (the object is
# byte for byte the same without it), but takes 2-3 times the memory of the rest
# of llc (up to 2.9 GB against 1.6 GB), which a small board does not have.
LLC_AARCH64 = ' -enable-machine-outliner=never'
env.Append(LLASMFLAGS=LLASM_PIC)
# a Linux binary asks for no glibc newer than glibc_max (scons/glibc_max.py), and
# finds the libSDL2_mixer beside itself first (the release has it: SteamOS has
# SDL2, not SDL2_mixer)
GLIBC_MAX = ARGUMENTS.get('glibc_max', CFG.get('glibc_max', glibc_max.DEFAULT))
glibc_max.version(GLIBC_MAX)
env['GLIBC_MAX'] = GLIBC_MAX
env['GLIBC_MAX_ARCH'] = 'aarch64' if device == 'arm64-linux' else 'x86_64'
GLIBC_MAX_RSP = os.path.join(STAGE_BUILD, 'obj', device, 'glibc_max.rsp')
if device in ('x64-linux', 'arm64-linux'):
    env.Append(LINKFLAGS='@' + GLIBC_MAX_RSP, RPATH=[env.Literal('\\$$ORIGIN')])
if device == 'x64-linux':
    env.Append(CCFLAGS='-DPTROFS_64BIT -fpie',
               CPPPATH=['/usr/include/SDL2', '/usr/local/include/SDL2'],
               LINKFLAGS='-pie -Wl,-z,noexecstack',
               LIBS=['SDL2_mixer', 'SDL2', 'pthread', 'dl', 'm'],
               LLVMFLAGS='-mtriple=x86_64-unknown-linux-gnu ' + LLC_PIC)
elif device == 'arm64-linux':
    # native aarch64 Linux: a PIE, as x64-linux.  The llasm -m64 is llasm's
    # own flag (64-bit pointers); llc gets the width from its triple.
    env.Append(CCFLAGS='-DPTROFS_64BIT -fpie',
               CPPPATH=['/usr/include/SDL2', '/usr/local/include/SDL2'],
               LINKFLAGS='-pie -Wl,-z,noexecstack',
               LIBS=['SDL2_mixer', 'SDL2', 'pthread', 'dl', 'm'],
               LLVMFLAGS='-mtriple=aarch64-unknown-linux-gnu ' + LLC_PIC + LLC_AARCH64)
elif device == 'arm64-android':
    # the NDK linker loads the .so anywhere and forbids text relocations
    env.Append(CCFLAGS='-fPIC -DPTROFS_64BIT',
               LIBS=['SDL2_mixer', 'SDL2', 'android', 'log'],
               LLVMFLAGS='-mtriple=aarch64-linux-android21 ' + LLC_PIC + LLC_AARCH64)
else:
    # a GUI program, as the other games: without a console main.c sends
    # stdout and stderr to a log in the write root, and shows its end
    # when the game stops on an error (the CD check's among them).
    # ASLR: dynamicbase + a .reloc section; high-entropy-va needs a 64-bit image.
    env.Append(CCFLAGS='-DPTROFS_64BIT -Dmain=SDL_main',
               LINKFLAGS='-mwindows -Wl,--dynamicbase,--enable-reloc-section,'
                         '--high-entropy-va,--nxcompat,--tsaware,'
                         # winpthread is linked statically: no libwinpthread-1.dll beside the exe.
                         # Whole, since libgcc's emulated TLS asks for pthread_* after all the -l
                         # of the line, and the implicit -lpthread would answer with the DLL.
                         '--whole-archive,-l:libwinpthread.a,--no-whole-archive',
               LIBS=['mingw32', 'SDL2main', 'SDL2_mixer', 'SDL2', 'winmm'],
               LLVMFLAGS='-mtriple=x86_64-pc-windows-gnu ' + LLC_PIC)

# extra flags for llasm of the game: llasm_flags=-guest-data-in-memory in
# game.cfg (the flags of a game that copies its data into the guest's memory)
if CFG.get('llasm_flags', ''):
    env.Append(LLASMFLAGS=' ' + CFG.get('llasm_flags', '').strip())
# guest_data_in_memory=yes: the game reaches its data at the addresses it had in
# the original (runtime/win32/winapi/Game_guestdata.c)
if WINAPI and CFG.get('guest_data_in_memory', 'no') == 'yes':
    env.Append(LLASMFLAGS=' -guest-data-in-memory', CPPDEFINES=['GAME_GUEST_DATA_IN_MEMORY'])

# extra libraries of the game for a device: libs.<device>=GL,... in game.cfg
env.Append(LIBS=[lib.strip() for lib in CFG.get('libs.' + device, '').split(',')
                 if lib.strip()])
GLES = GL and (device.endswith('android') or env['gles'])
if GLES:
    # OpenGL ES 3.0, linked directly: no glad, so what ES lacks does not compile
    env.Append(LIBS=['GLESv3' if device.endswith('android') else 'GLESv2'],
               CPPDEFINES=['GAME_GLES'])
elif GL:
    # OpenGL: glad (runtime/core/gl/glad.c) loads it
    env.Append(LIBS=['opengl32' if device.endswith('windows') else 'GL'])
if GL:
    env.Append(CPPDEFINES=['GAME_GL'])
if D3D_GL:
    env.Append(CPPDEFINES=['GAME_D3D_GL'])

# The port's namespace in the game's directory: the runtime's defaults name the
# settings file <program>.cfg and the log <program>.log (Game_config.c,
# Game_log.c), so the port never takes a name of the game's.
env.Append(CPPDEFINES=[('GAME_PROGRAM_NAME', '\\"%s\\"' % PROGRAM)])

for utility in ['opt', 'llc']:
    utility_path = shutil.which(utility)
    if utility_path is not None:
        utility_directory = os.path.dirname(utility_path)
        if utility_directory not in env['ENV']['PATH'].split(os.pathsep):
            env['ENV']['PATH'] += os.pathsep + utility_directory

# Fail before the first compile with the full list of what is missing, not
# deep in the obj stage when the shell reports "opt: not found".  Checked
# against the PATH of the actions (SCons gives them its own), which the loop
# above has already extended with the directory of opt and llc.
def tool_exists(tool):
    tool = env.subst(tool).split()[0]
    if os.path.isabs(tool):
        return os.path.exists(tool)
    return shutil.which(tool, path=env['ENV'].get('PATH')) is not None


missing = []
if want('obj'):
    if not tool_exists(env['CC']):
        missing.append('%s (C compiler of device %s)' % (env.subst(env['CC']), device))
    for utility in ('opt', 'llc'):
        if not tool_exists(utility):
            missing.append('%s (LLVM: llasm.py | opt | llc)' % utility)
if want('bin') and not tool_exists(env['LINK']):
    missing.append('%s (linker of device %s)' % (env.subst(env['LINK']), device))
if missing:
    hint = '  install LLVM (dnf install llvm / apt install llvm) and the C toolchain; ' \
           'a versioned opt-NN/llc-NN must also be on PATH as opt and llc'
    if device == 'x64-windows':
        hint = '  the Windows build is a cross build: install MinGW-w64 (dnf install mingw64-gcc / ' \
               'apt install gcc-mingw-w64-x86-64), or toolprefix= for another prefix; `make` alone ' \
               'skips it when there is none'
    raise Exception('toolchain of device %s: missing %s\n%s' % (device, ', '.join(missing), hint))

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


def disc_file(name):
    """A file of the game's disc tree, made by tools/disc_tree.py, found
    without regard to case: build/disc/<name>."""
    disc = os.path.join(BUILD, 'disc')
    if subprocess.call([sys.executable, '-I', os.path.join(SDK_ROOT, 'tools', 'disc_tree.py'), disc],
                       cwd=ROOT) != 0:
        raise Exception('stage 0: cannot make %s from game/distrib.cfg' % disc)
    path = disc
    for part in name.split('/'):
        entry = next((e for e in os.listdir(path) if e.lower() == part.lower()), None)
        if entry is None:
            raise Exception('stage 0: no %s in %s' % (name, disc))
        path = os.path.join(path, entry)
    return path


def shell_quote(path):
    return "'" + path.replace("'", "'\\''") + "'"


def module_pipeline(MODULE, EXT, input_key):
    """Stages input, recomp and gen of one module of the game: the main exe, or
    a DLL of dll_modules= (its own game/recomp/<module>/<variant>/, and its own
    build/{input,recomp,gen}/<module>/). Returns the generated master .llasm, its
    directory and what the master's object depends on."""
    global defaults
    RECOMP_SRC = os.path.join(GAME_DIR, 'recomp', MODULE, VARIANT)
    INPUT_DIR = os.path.join(STAGE_BUILD, 'input', MODULE)
    INPUT_EXE = os.path.join(INPUT_DIR, MODULE + EXT)
    RECOMP_DIR = os.path.join(STAGE_BUILD, 'recomp', MODULE, ARCH)
    GEN_SRC_DIR = os.path.join(STAGE_BUILD, 'gen', MODULE, ARCH)

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
    SR_SEGMENTS = os.path.join(RECOMP_DIR, 'segments.sha256')

    if want('input'):
        # Stage 0: the original, checked against the hashes the variant records
        # (sha256: one per line, the first word; several when releases of the game
        # share SR's inputs, as two languages of one build do).  The check runs here
        # rather than in build.sh so that it cannot be skipped: nothing downstream
        # ever reads an EXE that is not a recorded one.
        expected = set()
        with open(os.path.join(RECOMP_SRC, 'sha256'), 'rt') as stream:
            for line in stream:
                words = line.split('#', 1)[0].split()
                if words:
                    expected.add(words[0].lower())
        current = sha256_of(INPUT_EXE) if os.path.exists(INPUT_EXE) else None

        source = ARGUMENTS.get(input_key, CFG.get(input_key, ''))
        if not source and input_key != 'input_exe':
            source = 'disc:' + MODULE + EXT      # a DLL: the file of that name on the disc
        if source.startswith('disc:'):
            # the exe is a file of the game's disc, as game/distrib.cfg makes it
            # (the image with its patch= archives over it): build/disc/.  Looked
            # at every time: `scons image=` may have put another disc there.
            source = disc_file(source[len('disc:'):])
        elif current in expected and input_key not in ARGUMENTS:
            source = INPUT_EXE          # an original given by path is copied once
        elif source and not os.path.isabs(source):
            source = os.path.join(ROOT, source)
        if not source or not os.path.exists(source):
            raise Exception(
                'stage 0: %s is not there and no original to take it from '
                '(scons input_exe=/path/to/the/original, or game.cfg %s=%s)'
                % (INPUT_EXE, input_key, CFG.get(input_key, '')))
        actual = current if source == INPUT_EXE else sha256_of(source)
        if actual not in expected:
            raise Exception('stage 0: sha256 of %s is %s, expected %s (game/recomp/%s/%s/sha256)'
                            % (source, actual, ' or '.join(sorted(expected)), MODULE, VARIANT))
        if actual != current:
            if not os.path.isdir(INPUT_DIR):
                os.makedirs(INPUT_DIR)
            if os.path.exists(INPUT_EXE):
                os.remove(INPUT_EXE)
            shutil.copy2(source, INPUT_EXE)

    # What SR.cfg of a DOS4GW program holds whatever the game: the extender keeps
    # its stack and its work tables dword-aligned, and its flat DS lets the game
    # store into its code object (without code_writable SR's output makes the
    # code sections "constant").  Found by the extender's name in the exe (its
    # stub carries it).
    DOS4GW_SR_CFG = (
        '# SR-SDK: the defaults of a DOS4GW program (scons/game.py); the keys of the game follow\n'
        'esp_dword_aligned=yes\n'
        'ebp_dword_aligned=yes\n'
        'code_writable=yes\n')

    def make_sr_cfg(target, source, env):
        with open(str(source[0]), 'rb') as exe:
            is_dos4gw = PLATFORM == 'dos' and re.search(rb'(?i)dos/?4g', exe.read()) is not None
        with open(str(source[1])) as game_cfg:
            text = game_cfg.read()
        with open(str(target[0]), 'w') as out:
            out.write((DOS4GW_SR_CFG if is_dos4gw else '') + text)
            if MODULE in EXE_MODULES:
                out.write('\n# SR-SDK: a program of exe_modules= (scons/game.py)\nglobal_prefix=%s_\n' % MODULE.lower())

    if want('recomp'):
        # Stage 2: SR's working directory. SR.cfg, bssborder.csv and the .sci
        # files are read from the current directory, so the .sci files of
        # game/recomp/.../llasm/ land beside them here, the way build-llasm.sh
        # copies them in.
        # bssborder.csv is optional, SR goes on without it (SR_loader.c)
        if os.path.exists(os.path.join(RECOMP_SRC, 'bssborder.csv')):
            copy_if_newer(os.path.join(RECOMP_SRC, 'bssborder.csv'), os.path.join(RECOMP_DIR, 'bssborder.csv'))
        for sci in sorted(glob.glob(os.path.join(RECOMP_SRC, 'llasm', '*.sci'))):
            copy_if_newer(sci, os.path.join(RECOMP_DIR, os.path.basename(sci)))

        if not os.path.isdir(RECOMP_DIR):
            os.makedirs(RECOMP_DIR)

        sr_exe = os.path.join(HOST_SR, SR_EXE_NAME)
        sr_logs = [os.path.join(RECOMP_DIR, 'sr.out'), os.path.join(RECOMP_DIR, 'sr.err')]
        sr_targets = [RECOMP_ASM] + [os.path.join(RECOMP_DIR, name) for name in SR_EXTRAS] + sr_logs
        # Without a list scons does not know the seg*.llinc SR wrote, so a changed
        # segment would leave stage gen "up to date": their hashes stand for them.
        if not SR_EXTRAS:
            sr_targets.append(SR_SEGMENTS)
        # SR.cfg is the game's own, with the defaults of its extender before it
        # (a DOS4GW program: what every such game needs, which the game's file
        # may still override)
        sr_cfg = os.path.join(RECOMP_DIR, 'SR.cfg')
        defaults += env.Command(sr_cfg, [INPUT_EXE, File(os.path.join(RECOMP_SRC, 'SR.cfg'))],
                                Action(make_sr_cfg, 'SR.cfg  $TARGET'))
        sr_sources = [INPUT_EXE, File(sr_exe), File(sr_cfg)]
        if os.path.exists(os.path.join(RECOMP_SRC, 'bssborder.csv')):
            sr_sources.append(File(os.path.join(RECOMP_SRC, 'bssborder.csv')))
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

        if PLATFORM == 'dos' and ARCH == 'llasm':
            # SR finds the handlers the program gives to INT 21h AH=25h itself
            # (tools/sr_run.py: a second run takes them as code)
            game_fixup = os.path.join(RECOMP_SRC, 'llasm', 'fixup_interpret_as_code.sci')
            sr_command = 'cd %s && python3 %s %s %s %s %s' % (
                shell_quote(RECOMP_DIR), shell_quote(os.path.join(SDK_ROOT, 'tools', 'sr_run.py')),
                shell_quote(sr_exe), shell_quote(INPUT_EXE), shell_quote(MODULE + '.asm'), shell_quote(game_fixup))
        else:
            sr_command = 'cd %s && %s %s %s > sr.out 2> sr.err' % (
                shell_quote(RECOMP_DIR), shell_quote(sr_exe), shell_quote(INPUT_EXE), shell_quote(MODULE + '.asm'))
        if not SR_EXTRAS:
            sr_command += ' && { sha256sum seg*.llinc || :; } > %s' % shell_quote(os.path.basename(SR_SEGMENTS))
        if EXT == '.DLL':
            # DllMain is decoded as a procedure of its own (the alias SRW reads)
            sr_command = 'python3 %s alias %s %s %s && ' % (
                shell_quote(os.path.join(SDK_ROOT, 'tools', 'dll_exports.py')), MODULE,
                shell_quote(INPUT_EXE), shell_quote(os.path.join(RECOMP_DIR, 'global_aliases.sci'))) + sr_command
        defaults += env.Command(sr_targets, sr_sources, sr_command)

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
            + ([] if SR_EXTRAS else [SR_SEGMENTS]) + [File(compact)]
        if EXT == '.DLL':
            gen_sources.append(File(os.path.join(SDK_ROOT, 'tools', 'dll_exports.py')))

        # A game that lists its segments (compact / outputs) is built from that list.
        # Without one, every seg*.llinc SR wrote is taken: the list is what SR wrote.
        take_all_segments = '' if SR_EXTRAS else \
            ' && rm -f seg*.llinc && cp %s/seg*.llinc .' % shell_quote(RECOMP_DIR)
        gen_cmd = 'mkdir -p %s && cd %s && cp %s %s .%s && python3 compact_source.py --dialect llasm --module %s' % (
            shell_quote(GEN_SRC_DIR), shell_quote(GEN_SRC_DIR),
            shell_quote(compact),
            ' '.join(shell_quote(path) for path in [RECOMP_ASM]
                     + [os.path.join(RECOMP_DIR, name) for name in SR_EXTRAS]),
            take_all_segments,
            shell_quote(MODULE))
        if EXT == '.DLL':
            # the table the runtime loads the module by
            gen_cmd += ' && python3 %s table %s %s %s' % (
                shell_quote(os.path.join(SDK_ROOT, 'tools', 'dll_exports.py')), MODULE,
                shell_quote(INPUT_EXE), shell_quote(GEN_MAIN))
        defaults += env.Command(gen_targets, gen_sources, gen_cmd)

    gen_deps = [os.path.join(GEN_SRC_DIR, name) for name in SR_EXTRAS] if SR_EXTRAS else [SR_SEGMENTS]
    return GEN_MAIN, GEN_SRC_DIR, gen_deps


GEN_MAIN, GEN_SRC_DIR, gen_deps = module_pipeline(MODULE, '.EXE', 'input_exe')
# a DLL of the game recompiled as a module of its own: a master .llasm of its own
DLL_MASTERS = []
for DLL_MODULE in DLL_MODULES:
    DLL_MASTERS.append(module_pipeline(DLL_MODULE, '.DLL', 'input_' + DLL_MODULE))
# the other programs (exe_modules=) are built as the DLLs are
for EXE_MODULE in EXE_MODULES:
    DLL_MASTERS.append(module_pipeline(EXE_MODULE, '.EXE', 'input_' + EXE_MODULE))


# ------------------------------------------------- stage 4: obj, stage 5: bin --

if want('obj'):
    obj_device = os.path.join(STAGE_BUILD, 'obj', device)
    obj_game = os.path.join(obj_device, 'game')
    gen_dir = os.path.join(obj_device, 'gen')
    ir_dir = os.path.join(STAGE_BUILD, 'obj', 'ir')
    bin_device = os.path.join(BUILD, 'bin', device)

    # what game/llasm/SConscript asks for: where stage 3 put the generated
    # master, and where the hand-written includes it pulls in live, searched
    # in this order -- the game's own first, so a same-named file there wins
    gen_src_dir = GEN_SRC_DIR
    gen_main = GEN_MAIN
    # (gen_deps: what the master's object depends on, from module_pipeline)
    llasm_inc_dirs = LLASM_INC_DIRS

    # The game's sources, then the SDK's runtime, then SR's shared code. A .c
    # of the runtime that the game has under the same relative path is not
    # built (an override). Headers are looked up the same way for the game's
    # .c only: a .c of the runtime finds the runtime's headers beside it first.
    if WINAPI:
        env.Append(CPPDEFINES=['GAME_WINAPI'])
    if GLIDE:
        env.Append(CPPDEFINES=['GAME_GLIDE'])
    if DLL_MODULES:
        # the DLLs the runtime loads by name (WinApi-process.c): dll_modules.h has X(name) per module
        dll_header_dir = os.path.join(obj_device, 'include')
        os.makedirs(dll_header_dir, exist_ok=True)
        dll_header = '#define GAME_DLL_MODULES(X) ' + ' '.join('X(%s)' % m for m in DLL_MODULES) + '\n'
        dll_header_path = os.path.join(dll_header_dir, 'dll_modules.h')
        if not os.path.exists(dll_header_path) or open(dll_header_path).read() != dll_header:
            with open(dll_header_path, 'w') as stream:
                stream.write(dll_header)
        env.Append(CPPDEFINES=['GAME_HAVE_DLL_MODULES'], CPPPATH=[dll_header_dir])
    if EXE_MODULES:
        # the programs INT 21h AH=4Bh runs (Game_dosexec.c): X(NAME, prefix) per module
        exe_header_dir = os.path.join(obj_device, 'include')
        os.makedirs(exe_header_dir, exist_ok=True)
        exe_header = '#define GAME_EXE_MODULES(X) ' + ' '.join('X(%s, %s)' % (m, m.lower()) for m in EXE_MODULES) + '\n'
        exe_header_path = os.path.join(exe_header_dir, 'exe_modules.h')
        if not os.path.exists(exe_header_path) or open(exe_header_path).read() != exe_header:
            with open(exe_header_path, 'w') as stream:
                stream.write(exe_header)
        env.Append(CPPDEFINES=['GAME_HAVE_EXE_MODULES'], CPPPATH=[exe_header_dir])

    if CFG.get('icon') and device != 'arm64-android':
        # icon= of game.cfg: the window's icon, game_icon.h (tools/game_icon.py) for
        # Game_eventloop.c; the APK has it as the launcher's (tools/android_apk.py)
        icon_header_dir = os.path.join(obj_device, 'include')
        icon_tool = os.path.join(SDK_ROOT, 'tools', 'game_icon.py')
        env.Command(os.path.join(icon_header_dir, 'game_icon.h'),
                    [File(icon_tool), env.Value('icon=' + CFG['icon'])],
                    '"%s" -I "%s" --root "%s" --build "%s" "%s" --header "$TARGET"'
                    % (sys.executable, icon_tool, ROOT, BUILD, CFG['icon']))
        env.Append(CPPDEFINES=['GAME_HAVE_ICON'], CPPPATH=[icon_header_dir])

    env.Append(CPPPATH=[GAME_DIR] + RUNTIME_DIRS
               + [os.path.join(SDK_ROOT, 'extern', 'tsf'), os.path.join(SDK_ROOT, 'extern', 'emu8950')]
               + ([os.path.join(RUNTIME_CORE, 'gl'),
                   os.path.join(RUNTIME_CORE, 'gl', 'glad', 'include')] if GL else [])
               + [os.path.join(RUNTIME_CORE, 'display'), LLASM_SUPPORT])

    VariantDir(obj_game, GAME_DIR, duplicate=0)

    def game_dir(sub):
        # the game's SConscript of that directory if it has one, else all its .c
        if os.path.exists(os.path.join(GAME_DIR, sub, 'SConscript')):
            return SConscript(os.path.join(obj_game, sub, 'SConscript'),
                              exports='device env')
        return env.Object(Glob(os.path.join(obj_game, sub, '*.c')))

    # runtime files built as C, not C++ (the rest are C++): TinySoundFont's
    # static tables would need the C++ runtime (__cxa_guard_*), which the link
    # line does not have; emu8950 is C as well
    RUNTIME_PLAIN_C = ('Game_tsf_impl.c', 'Game_opl_impl.c', 'Game_opl_adpcm_impl.c')
    runtime_objs = []
    for runtime_dir in RUNTIME_DIRS:
        obj_runtime = os.path.join(obj_device, 'runtime',
                                   os.path.relpath(runtime_dir, os.path.join(SDK_ROOT, 'runtime')))
        VariantDir(obj_runtime, runtime_dir, duplicate=0)
        for sub in ('', 'display', 'audio', 'input') + (('gl',) if GL else ()):
            for source in sorted(glob.glob(os.path.join(runtime_dir, sub, '*.c'))):
                name = os.path.join(sub, os.path.basename(source))
                if (not os.path.exists(os.path.join(GAME_DIR, name))
                        and os.path.basename(source) not in RUNTIME_SKIP
                        and not (GLES and name == os.path.join('gl', 'glad.c'))):
                    if os.path.basename(source) == 'WinApi-stubs.c':
                        # one object per stub, in a library: only the stubs
                        # that the game does not define itself are linked
                        with open(source, 'rt') as stream:
                            stubs = re.findall(r'^#ifdef STUB_(\w+)$', stream.read(), re.M)
                        stub_objs = [env.Object(os.path.join(obj_runtime, 'stubs', stub + '.o'),
                                                os.path.join(obj_runtime, name),
                                                CPPDEFINES=['STUB_' + stub])[0]
                                     for stub in stubs]
                        env.StaticLibrary(os.path.join(obj_runtime, 'winapistubs'),
                                                     stub_objs)
                        env.Append(LIBPATH=[obj_runtime], LIBS=['winapistubs'])
                        continue
                    if os.path.basename(source) in RUNTIME_PLAIN_C:
                        runtime_objs += env.Object(os.path.join(obj_runtime, name),
                                                   CCFLAGS=env['CCFLAGS'] + ['-x', 'c', '-Wno-null-pointer-subtraction',
                                                                                 '-Wno-unused-parameter'])
                        continue
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
    if os.path.exists(os.path.join(GAME_DIR, 'llasm', 'SConscript')):
        game_objs += SConscript(os.path.join(obj_game, 'llasm', 'SConscript'),
                                exports='device env gen_dir ir_dir llasm_tool llasm_deps '
                                        'gen_src_dir gen_main llasm_inc_dirs')
    else:
        game_llasm_dir = os.path.join(obj_game, 'llasm')
        dll_masters = DLL_MASTERS
        sdk_llasm_dirs = SDK_LLASM_DIRS
        sdk_root = SDK_ROOT
        obj_runtime = os.path.join(obj_device, 'runtime')
        game_objs += SConscript(os.path.join(SDK_ROOT, 'scons', 'llasm.SConscript'),
                                exports='device env gen_dir ir_dir llasm_tool llasm_deps '
                                        'gen_src_dir gen_main gen_deps dll_masters llasm_inc_dirs game_llasm_dir '
                                        'sdk_llasm_dirs sdk_root obj_runtime')
    defaults += game_objs

program_nodes = None
if want('bin'):
    # The link order: the runtime's .c, the game's own .c, the three SDL
    # frontends, llasm-support, the game's llasm .c glue, then the .llasm
    # output of stage 3.
    if device == 'arm64-android':
        # SDLActivity loads libmain.so by name, so the shared object is
        # called main, not the program name from game.cfg.
        # (a target other than the main one keeps its own: an APK has one)
        defaults += env.SharedLibrary(os.path.join(STAGE_BUILD, 'bin', device, 'main'),
                                      game_objs)
    else:
        if device == 'x64-windows' and CFG.get('icon'):
            # icon= of game.cfg: the game's icon with the port's badge (tools/game_icon.py),
            # the exe's first icon resource, which Explorer and SDL's window show
            icon_dir = os.path.join(STAGE_BUILD, 'obj', device, 'icon')
            windres = 'windres' if env['PLATFORM'] == 'win32' else env['toolprefix'] + 'windres'
            icon_tool = os.path.join(SDK_ROOT, 'tools', 'game_icon.py')
            game_objs += env.Command(
                os.path.join(icon_dir, 'icon.o'),
                [File(icon_tool), env.Value('icon=' + CFG['icon'])],
                ['"%s" -I "%s" --root "%s" --build "%s" "%s" --ico "%s"'
                 % (sys.executable, icon_tool, ROOT, BUILD, CFG['icon'], os.path.join(icon_dir, 'icon.ico')),
                 Action(lambda target, source, env: open(os.path.join(icon_dir, 'icon.rc'), 'wt').write(
                     '1 ICON "icon.ico"\n') and None, 'icon.rc'),
                 'cd "%s" && %s -O coff -i icon.rc -o icon.o' % (icon_dir, windres)])
        program_nodes = env.Program(os.path.join(BUILD, 'bin', device, binary_name(PROGRAM, device)),
                                    game_objs)
        defaults += program_nodes
        if device in ('x64-linux', 'arm64-linux'):
            # glibc_max (scons/glibc_max.py): calls bound to old versions, then checked
            glibc_dir = os.path.join(STAGE_BUILD, 'obj', device)
            env.AddPreAction(program_nodes, Action(glibc_max.prepare(env, GLIBC_MAX_RSP,
                                                                     os.path.join(glibc_dir, 'glibc_max.S'),
                                                                     os.path.join(glibc_dir, 'glibc_max.o')),
                                                   'glibc_max $GLIBC_MAX: the calls of $TARGET'))
            env.AddPostAction(program_nodes, Action(glibc_max.check, 'glibc_max $GLIBC_MAX: check $TARGET'))
            env.Depends(program_nodes, env.Value('glibc_max=' + GLIBC_MAX))

# the programs of the other targets (targets= of game/game.cfg), for the archive
TARGET_PROGRAMS = []
if TARGET == 'default':
    for name in _keys(CFG, 'targets') if 'targets' in CFG else []:
        target_cfg = read_cfg(os.path.join(ROOT, 'game-' + name, 'game.cfg'))
        TARGET_PROGRAMS.append(target_cfg.get('program', '-'.join(_keys(target_cfg, 'modules')).lower()))

SConscript(os.path.join(SDK_ROOT, 'scons', 'release.SConscript'),
           exports='env device CFG ROOT BUILD SDK_ROOT PROGRAM program_nodes TARGET TARGET_PROGRAMS '
                   'binary_name')
Default(defaults)
