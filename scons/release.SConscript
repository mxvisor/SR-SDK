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
#     linux          the native binary and libSDL2_mixer beside it
#     windows        the MinGW cross build and the SDL DLLs beside the exe
#     disc           build/disc: the disc image with game/distrib.cfg over it
#     distrib-data   build/distrib/data: the game directory of the fullest install, with the music
#     distrib        build/distrib/<dist_name>-x64.zip
#     check          the native binary is there
#
# `distrib` packs binaries of two devices, so it does not build them: it is
# run after `linux` and `windows`, as game.mk does.  There is one archive for
# both systems, holding both programs: the native binary and the exe with its
# DLLs, the native binary with its libSDL2_mixer (the binary's RPATH is $ORIGIN:
# SteamOS has SDL2, not SDL2_mixer), every file of game/release/{linux,windows}/
# and their x64/ (configs, readmes; @GLIBC_MAX@ in a .txt is the binary's
# glibc_max, scons/glibc_max.py), <program>-NOTICE.txt (SR-SDK's LICENSE and
# NOTICE.md: the licenses of the code and the font in the programs and the SDL
# libraries beside them), and build/distrib/data.  There is no launcher script: the program
# is started as it is, and finds the game beside itself
# (runtime/core/Game_gamedir.c).
#
# game.cfg keys: program (the binary is <program>-x64, scons/game.py binary_name), dist_name (the archives' name, default
# program).  The data: game/distrib.cfg (SR-SDK/tools/distrib_cd.py).
#
# A repository with several targets (targets= of game/game.cfg, game-<name>/):
# a target other than the main one has only `linux` and `windows`; the main
# one makes the data and the archive, which holds the programs of all of them.

import glob
import hashlib
import os
import platform
import shutil
import stat
import subprocess
import sys
import zipfile

Import('env device CFG ROOT BUILD SDK_ROOT PROGRAM program_nodes TARGET TARGET_PROGRAMS binary_name')

DIST = os.path.join(BUILD, 'distrib')
DIST_NAME = CFG.get('dist_name', PROGRAM)
DISTRIB_CFG = os.path.join(ROOT, 'game', 'distrib.cfg')
DISTRIB_DIR = os.path.join(DIST, 'data')
DISTRIB_STAMP = os.path.join(DIST, 'data.stamp')
RELEASE_DIR = os.path.join(ROOT, 'game', 'release')
SDL_DLLS = ('SDL2.dll', 'SDL3.dll', 'SDL2_mixer.dll')    # SDL2.dll is sdl2-compat and loads SDL3.dll
LINUX_DEVICE = 'arm64-linux' if platform.machine().lower() in ('aarch64', 'arm64') else 'x64-linux'
WINDOWS_BIN_DIR = os.path.join(BUILD, 'bin', 'x64-windows')
LINUX_BIN_DIR = os.path.join(BUILD, 'bin', LINUX_DEVICE)
LINUX_SOS = ('libSDL2_mixer-2.0.so.0', )
NOTICE_SOURCES = (os.path.join(SDK_ROOT, 'LICENSE'), os.path.join(SDK_ROOT, 'NOTICE.md'))
NOTICE_NAME = PROGRAM + '-NOTICE.txt'


# -------------------------------------------------------- linux, windows --

if program_nodes is not None:
    if device == LINUX_DEVICE:
        # the library the linker found, under its soname
        sos = []
        for so in LINUX_SOS:
            found = subprocess.run([env.subst('$CC'), '-print-file-name=' + so], check=True,
                                   capture_output=True, text=True).stdout.strip()
            if not os.path.isabs(found) or not os.path.isfile(found):
                raise Exception('linux: no %s (the SDL2_mixer package)' % so)
            sos += env.Command(os.path.join(LINUX_BIN_DIR, so), os.path.realpath(found),
                               Copy('$TARGET', '$SOURCE'))
        env.Alias('linux', program_nodes + sos)
    elif device == 'x64-windows':
        # the MinGW runtime beside the exe, so build/bin/x64-windows/ is self-contained
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

# ------------------------------------------------- disc, distrib-data --

if TARGET != 'default':
    Return()

if os.path.isfile(DISTRIB_CFG):
    def make_disc(target, source, env):
        subprocess.run([sys.executable, '-I', os.path.join(SDK_ROOT, 'tools', 'disc_tree.py'),
                        os.path.join(BUILD, 'disc')], cwd=ROOT, check=True)

    env.Alias('disc', env.AlwaysBuild(env.Command(os.path.join(DIST, 'disc.stamp'),
                                                  DISTRIB_CFG, make_disc)))

    def inputs_hash():
        digest = hashlib.sha256()
        for path in (DISTRIB_CFG, os.path.join(SDK_ROOT, 'tools', 'distrib_cd.py')):
            with open(path, 'rb') as stream:
                digest.update(stream.read())
        return digest.hexdigest()

    def make_distrib(target, source, env):
        # SCons's database is not kept by `clean`: the stamp says what the directory was made from
        # (SCons removes the target before the action: the stamp was read when the graph was made)
        if os.path.isdir(DISTRIB_DIR) and OLD_STAMP.split()[-1:] == [inputs_hash()]:
            print('==> Distrib data: %s is up to date' % DISTRIB_DIR)
            return
        for tool in ('python3', 'ffmpeg'):
            if shutil.which(tool) is None:
                raise Exception('distrib-data: %s not found' % tool)
        print('==> Distrib data: making %s from the disc of game/distrib.cfg' % DISTRIB_DIR)
        shutil.rmtree(DISTRIB_DIR, ignore_errors=True)
        subprocess.run([sys.executable, os.path.join(SDK_ROOT, 'tools', 'distrib_cd.py'), DISTRIB_DIR],
                       cwd=ROOT, check=True)
        with open(str(target[0]), 'wt') as stamp:
            stamp.write('made from game/distrib.cfg %s\n' % inputs_hash())

    OLD_STAMP = open(DISTRIB_STAMP).read() if os.path.isfile(DISTRIB_STAMP) else ''
    # the stamp stands for the directory: when it is gone, so is the stamp
    if not os.path.isdir(DISTRIB_DIR) and os.path.exists(DISTRIB_STAMP):
        os.remove(DISTRIB_STAMP)
    # made again when the cfg or the tool that reads it changes
    env.Alias('distrib-data', env.Command(DISTRIB_STAMP, [DISTRIB_CFG, os.path.join(SDK_ROOT, 'tools', 'distrib_cd.py')],
                                           make_distrib))

# ------------------------------------------------------------------ distrib --

def port_names(binaries, dlls, release_oses):
    """The files of the port in the game's directory: the programs, the DLLs and
    the release files, and the settings file, the log and the registry (a Win32
    game's) the runtime names after the program (scons/game.py's GAME_PROGRAM_NAME)."""
    names = [os.path.basename(binary) for binary in binaries]
    names += [os.path.basename(dll) for dll in dlls]
    names.append(NOTICE_NAME)
    for release_os in release_oses:
        names += [os.path.basename(source) for source in release_files(release_os)]
    for program in [PROGRAM] + TARGET_PROGRAMS:
        names += [program + '.cfg', program + '.log', program + '.reg']
    return names


def check_names(directory, names):
    """A game's directory is the port's too, and Windows has one file for names
    that differ only in case: a port's file must not be one of the game's."""
    game = {}
    for entry in os.listdir(DISTRIB_DIR):
        game.setdefault(entry.lower(), entry)
    clashes = ['%s (the port) / %s (the game)' % (name, game[name.lower()])
               for name in names if name.lower() in game]
    if clashes:
        raise Exception('release: the port\'s files of %s collide with the game\'s '
                        '(one file on a case-insensitive file system): %s; '
                        'change the name in game.cfg (program=)'
                        % (os.path.basename(directory), ', '.join(sorted(clashes))))


def stage_directory(name, binaries, dlls, release_oses, with_data=True):
    """build/distrib/<name>/: the programs, the release files and, with_data, the
    game's data."""
    directory = os.path.join(DIST, name)
    shutil.rmtree(directory, ignore_errors=True)
    os.makedirs(directory)
    # The data first: a game may install a file of the same name as the program
    # (the original's exe in another case), and the programs of the release must
    # be the port's, not covered by it.
    if os.path.isdir(DISTRIB_DIR):
        check_names(directory, port_names(binaries, dlls, release_oses))
        if with_data:
            shutil.copytree(DISTRIB_DIR, directory, symlinks=True, dirs_exist_ok=True)
    for release_os in release_oses:
        for source in release_files(release_os):
            shutil.copy2(source, directory)
            if source.endswith('.txt'):
                path = os.path.join(directory, os.path.basename(source))
                with open(path, 'rb') as stream:
                    text = stream.read()
                with open(path, 'wb') as stream:
                    stream.write(text.replace(b'@GLIBC_MAX@', env['GLIBC_MAX'].encode()))
    with open(os.path.join(directory, NOTICE_NAME), 'wb') as stream:
        stream.write(b'\n\n'.join(open(path, 'rb').read().rstrip() for path in NOTICE_SOURCES) + b'\n')
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


def pack_archive(target, source, env, with_data=True):
    name = os.path.splitext(os.path.basename(str(target[0])))[0]
    programs = 1 + len(TARGET_PROGRAMS)
    binaries = [str(node) for node in source[:2 * programs]]
    dlls = sorted(glob.glob(os.path.join(WINDOWS_BIN_DIR, '*.dll'))) \
        + [os.path.join(LINUX_BIN_DIR, so) for so in LINUX_SOS]
    directory = stage_directory(name, binaries, dlls, ('linux', 'windows'), with_data)
    with zipfile.ZipFile(str(target[0]), 'w', zipfile.ZIP_DEFLATED) as archive:
        for base, dirs, files in os.walk(directory):
            dirs.sort()
            if not dirs and not files and base != directory:
                # an empty directory the game writes into (a save directory): unzip
                # makes it only from an entry of its own
                archive.write(base, os.path.join(name, os.path.relpath(base, directory)))
            for file in sorted(files):
                path = os.path.join(base, file)
                archive.write(path, os.path.join(name, os.path.relpath(path, directory)))


def linux_file(program):
    return File(os.path.join(LINUX_BIN_DIR, binary_name(program, LINUX_DEVICE)))


def windows_file(program):
    return File(os.path.join(WINDOWS_BIN_DIR, binary_name(program, 'x64-windows') + '.exe'))


linux_binary = program_nodes if (program_nodes is not None and device == LINUX_DEVICE) \
    else [linux_file(PROGRAM)]
windows_binary = windows_file(PROGRAM)
# the programs of the other targets: built by their own scons (game.mk), not here
target_binaries = [linux_file(program) for program in TARGET_PROGRAMS] \
    + [windows_file(program) for program in TARGET_PROGRAMS]
dlls = [File(os.path.join(WINDOWS_BIN_DIR, dll)) for dll in SDL_DLLS] \
    + [File(os.path.join(LINUX_BIN_DIR, so)) for so in LINUX_SOS]
data = [File(DISTRIB_STAMP)] if os.path.isfile(DISTRIB_CFG) else []
release = release_files('linux') + release_files('windows')
# scons compares the sources by their contents: the names are a source too, or a
# renamed release file leaves the archive with the old name
names = env.Value(' '.join(os.path.relpath(path, RELEASE_DIR) for path in release)
                  + ' glibc_max=' + env['GLIBC_MAX'])
archive = env.Command(os.path.join(DIST, '%s-x64.zip' % DIST_NAME),
                      linux_binary + target_binaries[:len(TARGET_PROGRAMS)] + [windows_binary]
                      + target_binaries[len(TARGET_PROGRAMS):] + dlls + data + release
                      + [File(path) for path in NOTICE_SOURCES] + [names],
                      pack_archive)
env.Alias('distrib', archive)
# the port alone, for an installed game: the archive of distrib without the game's data,
# which the player has from the game's own install
port_archive = env.Command(os.path.join(DIST, '%s-x64-port.zip' % DIST_NAME),
                           linux_binary + target_binaries[:len(TARGET_PROGRAMS)] + [windows_binary]
                           + target_binaries[len(TARGET_PROGRAMS):] + dlls + release
                           + [File(path) for path in NOTICE_SOURCES] + [names],
                           lambda target, source, env: pack_archive(target, source, env, False))
env.Alias('release', port_archive)
