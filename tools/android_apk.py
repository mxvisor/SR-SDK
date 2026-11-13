#! /usr/bin/env python3
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

# The Android APK of a game, run from the game repository's root:
#
#     android_apk.py [--data <the game's data dir>] [--out <file.apk>] [--target <name>]
#
# --target: one target of a repository of several (game-<name>/ in place of game/,
# scons/game.py), with its own game.cfg and libmain.so.
#
# The defaults are what `make apk` (scons distrib-data) leaves: the data is
# <build>/distrib/data, the APK <build>/distrib/<dist_name>-android-arm64.apk
# (dist_name= of game.cfg, default the program's name).
#
# 1. SDL2 and SDL2_mixer for arm64, by tools/android_sdl.sh, unless there;
# 2. libmain.so, by scons device=arm64-android (libgameloader.so, which loads it, here);
# 3. <build>/android/jniLibs/, assets/game/ and assets/NOTICE.txt: the .so files,
#    the data, SR-SDK's LICENSE and NOTICE.md;
# 4. <build>/android/project/: SDL's android-project of the same SDL release,
#    with android/app.gradle, android/GameActivity.java, the game's name from
#    game.cfg and the game's game/android/res/ over it;
# 5. gradle assembleDebug, all of its output in <build>/android/.
#
# game/game.cfg:
#     android_id=io.github.<user>.<game>     the application id (required)
#     android_name=<Name>                    the launcher's label (default:
#                                            program)
#
# The Android SDK is $ANDROID_HOME (or $ANDROID_SDK_ROOT); the NDK is
# $ANDROID_NDK_HOME or the newest <sdk>/ndk/*; Java is $JAVA_HOME or <sdk>/jdk.

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
from typing import NoReturn
from xml.sax.saxutils import escape

SDK_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACTIVITY_PACKAGE = 'io.github.mxvisor.srsdk'


def fail(message) -> NoReturn:
    sys.exit('android_apk: ' + message)


def read_cfg(path):
    # as scons/game.py reads it
    cfg = {}
    with open(path, 'rt') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if line:
                key, value = line.split('=', 1)
                cfg[key.strip()] = value.strip()
    return cfg


def sdl_version():
    with open(os.path.join(SDK_ROOT, 'tools', 'android_sdl.sh'), 'rt') as stream:
        match = re.search(r'^SDL_VERSION=(\S+)$', stream.read(), re.M)
    if not match:
        fail('android_sdl.sh has no SDL_VERSION')
    return match.group(1)


def link_or_copy(source, destination):
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


def run(command, **kwargs):
    print('+ ' + ' '.join(command), flush=True)
    subprocess.run(command, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description='Builds the Android APK of a game.')
    parser.add_argument('--data', help="the game's data directory (default <build>/distrib/data)")
    parser.add_argument('--out', help='the APK to write (default <build>/distrib/<dist_name>-android-arm64.apk)')
    parser.add_argument('--build', default='build', help='the build directory (default build)')
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--target', default='default',
                        help='the target of a repository of several (game-<target>/, scons/game.py; default the main one, game/)')
    args = parser.parse_args()

    root = os.getcwd()
    build = os.path.abspath(args.build)
    game_dir = os.path.join(root, 'game' if args.target == 'default' else 'game-' + args.target)
    stage_build = build if args.target == 'default' else os.path.join(build, 'target-' + args.target)
    cfg = read_cfg(os.path.join(game_dir, 'game.cfg'))
    data = os.path.abspath(args.data or os.path.join(build, 'distrib', 'data'))
    out = os.path.abspath(args.out or os.path.join(
        build, 'distrib', '%s-android-arm64.apk' % (cfg.get('dist_name') or cfg.get('program') or 'game')))
    app_id = cfg.get('android_id') or fail('game.cfg has no android_id')
    app_name = cfg.get('android_name') or cfg.get('program') or app_id
    if not os.path.isdir(data):
        fail('no data directory ' + data)

    sdk = os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
    if not sdk or not os.path.isdir(sdk):
        fail('set ANDROID_HOME to the Android SDK')
    ndk = os.environ.get('ANDROID_NDK_HOME')
    if not ndk:
        ndks = sorted(glob.glob(os.path.join(sdk, 'ndk', '*')))
        if not ndks:
            fail('no NDK in %s/ndk, or set ANDROID_NDK_HOME' % sdk)
        ndk = ndks[-1]
    java = os.environ.get('JAVA_HOME') or os.path.join(sdk, 'jdk')
    if not os.path.isfile(os.path.join(java, 'bin', 'java')):
        fail('set JAVA_HOME (no %s/bin/java)' % java)
    env = dict(os.environ, ANDROID_HOME=sdk, ANDROID_NDK_HOME=ndk, JAVA_HOME=java,
               PATH=os.path.join(java, 'bin') + os.pathsep + os.environ.get('PATH', ''))

    # 1. SDL
    sysroot = os.path.join(build, 'sysroot-android-arm64', 'usr')
    version = sdl_version()
    sdl_project = os.path.join(build, 'android-sdl', 'SDL2-' + version, 'android-project')
    if not (os.path.isfile(os.path.join(sysroot, 'lib', 'libSDL2.so'))
            and os.path.isdir(sdl_project)):
        run(['sh', os.path.join(SDK_ROOT, 'tools', 'android_sdl.sh'), build], env=env)

    # 2. libmain.so
    run(['scons', 'stage=bin', 'device=arm64-android', '-j%d' % args.jobs,
         'BUILD_DIR=' + build, 'target=' + args.target], env=env)

    # 3. jniLibs and assets
    android = os.path.join(build, 'android')
    jni = os.path.join(android, 'jniLibs', 'arm64-v8a')
    shutil.rmtree(os.path.join(android, 'jniLibs'), ignore_errors=True)
    os.makedirs(jni)
    ndk_lib = os.path.join(ndk, 'toolchains', 'llvm', 'prebuilt', 'linux-x86_64',
                           'sysroot', 'usr', 'lib', 'aarch64-linux-android')
    clang = os.path.join(ndk, 'toolchains', 'llvm', 'prebuilt', 'linux-x86_64',
                         'bin', 'aarch64-linux-android21-clang')
    run([clang, '-shared', '-fPIC', '-O2', '-Wall', '-o', os.path.join(jni, 'libgameloader.so'),
         os.path.join(SDK_ROOT, 'android', 'gameloader.c'), '-ldl'], env=env)
    for library in (os.path.join(stage_build, 'bin', 'arm64-android', 'libmain.so'),
                    os.path.join(sysroot, 'lib', 'libSDL2.so'),
                    os.path.join(sysroot, 'lib', 'libSDL2_mixer.so'),
                    os.path.join(ndk_lib, 'libc++_shared.so')):
        shutil.copy2(library, jni)
    assets = os.path.join(android, 'assets')
    shutil.rmtree(assets, ignore_errors=True)
    shutil.copytree(data, os.path.join(assets, 'game'), copy_function=link_or_copy)
    # the licenses of the code and the font in libmain.so and of SDL
    with open(os.path.join(assets, 'NOTICE.txt'), 'wb') as stream:
        stream.write(b'\n\n'.join(open(os.path.join(SDK_ROOT, name), 'rb').read().rstrip()
                                   for name in ('LICENSE', 'NOTICE.md')) + b'\n')

    # 4. the gradle project
    project = os.path.join(android, 'project')
    shutil.rmtree(project, ignore_errors=True)
    shutil.copytree(sdl_project, project,
                    ignore=shutil.ignore_patterns('jni', 'build.gradle'))
    shutil.copy2(os.path.join(sdl_project, 'build.gradle'), project)
    shutil.copy2(os.path.join(SDK_ROOT, 'android', 'app.gradle'),
                 os.path.join(project, 'app', 'build.gradle'))
    java_dir = os.path.join(project, 'app', 'src', 'main', 'java', *ACTIVITY_PACKAGE.split('.'))
    os.makedirs(java_dir)
    shutil.copy2(os.path.join(SDK_ROOT, 'android', 'GameActivity.java'), java_dir)

    manifest = os.path.join(project, 'app', 'src', 'main', 'AndroidManifest.xml')
    with open(manifest, 'rt') as stream:
        text = stream.read()
    text, count = re.subn(r'android:name="SDLActivity"', 'android:name=".GameActivity"', text)
    if count != 1:
        fail("SDL's AndroidManifest.xml: no SDLActivity to replace")
    # landscape only, flipping by 180 degrees with the sensor (also with the auto-rotation
    # of the phone off): the games have no portrait picture and the touch layout is a landscape one
    text, count = re.subn(r'(<activity android:name="\.GameActivity")', r'\1\n            android:screenOrientation="sensorLandscape"', text)
    if count != 1:
        fail("SDL's AndroidManifest.xml: no activity to give an orientation")
    # SDL's template asks to be started when a USB device is plugged in
    text = re.sub(r'\s*<!-- Let Android know that we can handle some USB devices[^>]*-->'
                  r'\s*<intent-filter>\s*<action android:name="android\.hardware\.usb\.'
                  r'action\.USB_DEVICE_ATTACHED" />\s*</intent-filter>', '', text)
    with open(manifest, 'wt') as stream:
        stream.write(text)

    # SDL 2.30's wrapper is gradle 8.1.1, which does not run on Java 21
    wrapper = os.path.join(project, 'gradle', 'wrapper', 'gradle-wrapper.properties')
    with open(wrapper, 'rt') as stream:
        text = stream.read()
    text = re.sub(r'gradle-[0-9.]+-bin\.zip', 'gradle-8.5-bin.zip', text)
    with open(wrapper, 'wt') as stream:
        stream.write(text)

    res = os.path.join(project, 'app', 'src', 'main', 'res')
    with open(os.path.join(res, 'values', 'strings.xml'), 'wt') as stream:
        stream.write('<resources>\n    <string name="app_name">%s</string>\n</resources>\n'
                     % escape(app_name))
    game_res = os.path.join(game_dir, 'android', 'res')
    if os.path.isdir(game_res):
        shutil.copytree(game_res, res, dirs_exist_ok=True)

    # 5. gradle
    run(['./gradlew', '--project-cache-dir', os.path.join(android, 'gradle-cache'),
         '-PgameApplicationId=' + app_id, 'assembleDebug', '--no-daemon'],
        cwd=project, env=env)
    apk = os.path.join(android, 'gradle', 'outputs', 'apk', 'debug', 'app-debug.apk')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    shutil.copy2(apk, out)
    print('OK: ' + out)


if __name__ == '__main__':
    main()
