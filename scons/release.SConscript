#
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

# The named targets of a game, over the graph of game.py (the game's Makefile
# has them as `make <name>`, through scons/game.mk):
#
#     linux          the native binary
#     windows        the MinGW cross build and the SDL DLLs beside the exe
#     disc           build/disc: the disc image with game/portable.cfg over it
#     portable-data  build/dist/portable: the game directory of the fullest install, with the music
#     portable       build/dist/<dist_name>-x64.zip
#     check          the native binary is there
#
# `portable` packs binaries of two devices, so it does not build them: it is
# run after `linux` and `windows`, as game.mk does.  There is one archive for
# both systems, holding both programs: the native binary and the exe with its
# DLLs, every file of game/release/{linux,windows}/ and their x64/ (configs,
# readmes), and build/dist/portable.  There is no launcher script: the program
# is started as it is, and finds the game beside itself
# (runtime/core/Game_gamedir.c).
#
# game.cfg keys: program (the binary), dist_name (the archives' name, default
# program).  The data: game/portable.cfg (SR-SDK/tools/portable_cd.py).

import glob
import os
import platform
import shutil
import stat
import subprocess
import sys
import zipfile

Import('env device CFG ROOT BUILD SDK_ROOT PROGRAM program_nodes')

DIST = os.path.join(BUILD, 'dist')
DIST_NAME = CFG.get('dist_name', PROGRAM)
PORTABLE_CFG = os.path.join(ROOT, 'game', 'portable.cfg')
PORTABLE_DIR = os.path.join(DIST, 'portable')
PORTABLE_STAMP = os.path.join(DIST, 'portable.stamp')
RELEASE_DIR = os.path.join(ROOT, 'game', 'release')
SDL_DLLS = ('SDL2.dll', 'SDL3.dll', 'SDL2_mixer.dll')    # SDL2.dll is sdl2-compat and loads SDL3.dll
LINUX_DEVICE = 'arm64-linux' if platform.machine().lower() in ('aarch64', 'arm64') else 'pc64-linux'
WINDOWS_BIN_DIR = os.path.join(BUILD, 'bin', 'pc64-windows')


# -------------------------------------------------------- linux, windows --

if program_nodes is not None:
    if device == LINUX_DEVICE:
        env.Alias('linux', program_nodes)
    elif device == 'pc64-windows':
        # the MinGW runtime beside the exe, so build/bin/pc64-windows/ is self-contained
        mingw_bin = os.environ.get('MINGW_BIN') or '/usr/%s/sys-root/mingw/bin' % env['toolprefix'].rstrip('-')
        dlls = []
        for dll in SDL_DLLS:
            if not os.path.isfile(os.path.join(mingw_bin, dll)):
                raise Exception('windows: no %s in %s (MINGW_BIN)' % (dll, mingw_bin))
            dlls += env.Command(os.path.join(WINDOWS_BIN_DIR, dll),
                                os.path.join(mingw_bin, dll), Copy('$TARGET', '$SOURCE'))
        env.Alias('windows', program_nodes + dlls)


def check_binary(target, source, env):
    path = str(source[0])
    if not os.path.isfile(path):
        raise Exception('check: no %s' % path)
    print('OK: binary exists: %s' % path)


if program_nodes is not None and device == LINUX_DEVICE:
    env.Alias('check', env.AlwaysBuild(env.Command(os.path.join(DIST, 'check.stamp'),
                                                   program_nodes, check_binary)))

# ------------------------------------------------- disc, portable-data --

if os.path.isfile(PORTABLE_CFG):
    def make_disc(target, source, env):
        subprocess.run([sys.executable, '-I', os.path.join(SDK_ROOT, 'tools', 'disc_tree.py'),
                        os.path.join(BUILD, 'disc')], cwd=ROOT, check=True)

    env.Alias('disc', env.AlwaysBuild(env.Command(os.path.join(DIST, 'disc.stamp'),
                                                  PORTABLE_CFG, make_disc)))

    def make_portable(target, source, env):
        for tool in ('python3', 'ffmpeg'):
            if shutil.which(tool) is None:
                raise Exception('portable-data: %s not found' % tool)
        print('==> Portable data: making %s from the disc of game/portable.cfg' % PORTABLE_DIR)
        shutil.rmtree(PORTABLE_DIR, ignore_errors=True)
        subprocess.run([sys.executable, os.path.join(SDK_ROOT, 'tools', 'portable_cd.py'), PORTABLE_DIR],
                       cwd=ROOT, check=True)
        with open(str(target[0]), 'wt') as stamp:
            stamp.write('made from game/portable.cfg\n')

    # the stamp stands for the directory: when it is gone, so is the stamp
    if not os.path.isdir(PORTABLE_DIR) and os.path.exists(PORTABLE_STAMP):
        os.remove(PORTABLE_STAMP)
    # made again when the cfg or the tool that reads it changes
    env.Alias('portable-data', env.Command(PORTABLE_STAMP, [PORTABLE_CFG, os.path.join(SDK_ROOT, 'tools', 'portable_cd.py')],
                                           make_portable))

# ----------------------------------------------------------------- portable --

def port_names(binaries, dlls, release_oses):
    """The files of the port in the game's directory: the programs, the DLLs and
    the release files, and the settings file, the log and the registry (a Win32
    game's) the runtime names after the program (scons/game.py's GAME_PROGRAM_NAME)."""
    names = [os.path.basename(binary) for binary in binaries]
    names += [os.path.basename(dll) for dll in dlls]
    for release_os in release_oses:
        names += [os.path.basename(source) for source in release_files(release_os)]
    names += [PROGRAM + '.cfg', PROGRAM + '.log', PROGRAM + '.reg']
    return names


def check_names(directory, names):
    """A game's directory is the port's too, and Windows has one file for names
    that differ only in case: a port's file must not be one of the game's."""
    game = {}
    for entry in os.listdir(PORTABLE_DIR):
        game.setdefault(entry.lower(), entry)
    clashes = ['%s (the port) / %s (the game)' % (name, game[name.lower()])
               for name in names if name.lower() in game]
    if clashes:
        raise Exception('release: the port\'s files of %s collide with the game\'s '
                        '(one file on a case-insensitive file system): %s; '
                        'change the name in game.cfg (program=)'
                        % (os.path.basename(directory), ', '.join(sorted(clashes))))


def stage_directory(name, binaries, dlls, release_oses):
    """build/dist/<name>/: the programs, the release files, the data."""
    directory = os.path.join(DIST, name)
    shutil.rmtree(directory, ignore_errors=True)
    os.makedirs(directory)
    # The data first: a game may install a file of the same name as the program
    # (the original's exe in another case), and the programs of the release must
    # be the port's, not covered by it.
    if os.path.isdir(PORTABLE_DIR):
        check_names(directory, port_names(binaries, dlls, release_oses))
        shutil.copytree(PORTABLE_DIR, directory, symlinks=True, dirs_exist_ok=True)
    for release_os in release_oses:
        for source in release_files(release_os):
            shutil.copy2(source, directory)
    for dll in dlls:
        shutil.copy2(dll, directory)
    for binary in binaries:
        shutil.copy2(binary, directory)
    for binary in binaries:
        path = os.path.join(directory, os.path.basename(binary))
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return directory


def release_files(release_os):
    files = []
    for directory in (os.path.join(RELEASE_DIR, release_os), os.path.join(RELEASE_DIR, release_os, 'x64')):
        files += sorted(path for path in glob.glob(os.path.join(directory, '*')) if os.path.isfile(path))
    return files


def pack_archive(target, source, env):
    name = '%s-x64' % DIST_NAME
    linux_binary = str(source[0])
    windows_binary = str(source[1])
    dlls = sorted(glob.glob(os.path.join(os.path.dirname(windows_binary), '*.dll')))
    directory = stage_directory(name, [linux_binary, windows_binary], dlls, ('linux', 'windows'))
    with zipfile.ZipFile(str(target[0]), 'w', zipfile.ZIP_DEFLATED) as archive:
        for base, dirs, files in os.walk(directory):
            dirs.sort()
            for file in sorted(files):
                path = os.path.join(base, file)
                archive.write(path, os.path.join(name, os.path.relpath(path, directory)))


linux_binary = program_nodes if (program_nodes is not None and device == LINUX_DEVICE) \
    else [File(os.path.join(BUILD, 'bin', LINUX_DEVICE, PROGRAM))]
windows_binary = File(os.path.join(WINDOWS_BIN_DIR, PROGRAM + '.exe'))
dlls = [File(os.path.join(WINDOWS_BIN_DIR, dll)) for dll in SDL_DLLS]
data = [File(PORTABLE_STAMP)] if os.path.isfile(PORTABLE_CFG) else []
release = release_files('linux') + release_files('windows')
# scons compares the sources by their contents: the names are a source too, or a
# renamed release file leaves the archive with the old name
names = env.Value(' '.join(os.path.relpath(path, RELEASE_DIR) for path in release))
archive = env.Command(os.path.join(DIST, '%s-x64.zip' % DIST_NAME),
                      linux_binary + [windows_binary] + dlls + data + release + [names],
                      pack_archive)
env.Alias('portable', archive)
