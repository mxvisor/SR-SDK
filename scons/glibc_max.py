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

# glibc_max (game.cfg key or `scons glibc_max=2.38`): the newest glibc a Linux
# binary may ask for, so that it runs on systems older than the one it is built
# on (the Steam Deck's SteamOS among them).
#
# glibc keeps the old versions of a function when it changes one (sqrtf and
# log10f got GLIBC_2.43 in glibc 2.43, and have GLIBC_2.2.5 still), and the
# linker takes the newest.  Before the link, prepare() finds the functions of
# libc and libm that the objects call and whose newest version is above
# glibc_max, and sends every call to the newest version not above it: a
# trampoline `__wrap_<name>` that jumps to `<name>@GLIBC_<old>` (.symver), and
# `-Wl,--wrap=<name>` (the response file of the link line), which binds the
# calls of every object, those of llc's too, to the trampoline.  After the
# link, check() fails the build when the binary still asks for a newer glibc:
# a function with no older version (__isoc23_strtol is 2.38 and nothing else).

import os
import re
import subprocess

DEFAULT = '2.38'

_LINE = re.compile(r'\s(\()?GLIBC_([0-9.]+)\)?\s+(\S+)$')


def version(text):
    return tuple(int(part) for part in text.split('.'))


def _symbols(path):
    """{name: (default version, [all versions])} of a shared library."""
    out = subprocess.run(['objdump', '-T', path], check=True, capture_output=True, text=True).stdout
    table = {}
    for line in out.splitlines():
        if ' DF ' not in line and ' iD ' not in line and ' DO ' not in line:
            continue
        match = _LINE.search(line)
        if match is None or '*UND*' in line:
            continue
        hidden, ver, name = match.groups()
        entry = table.setdefault(name, [None, []])
        entry[1].append(version(ver))
        if not hidden:
            entry[0] = version(ver)
    return table


def _library(env, name):
    path = subprocess.run([env.subst('$CC'), '-print-file-name=' + name], check=True,
                          capture_output=True, text=True).stdout.strip()
    return os.path.realpath(path)


def _undefined(paths):
    names = set()
    for path in paths:
        if not path.endswith(('.o', '.a')):
            continue
        out = subprocess.run(['nm', '-uP', path], check=True, capture_output=True, text=True).stdout
        names.update(line.split()[0] for line in out.splitlines() if line.strip())
    return names


def prepare(env, response, asm, obj):
    """The pre-action of the program: the trampolines and the response file."""
    limit = version(env['GLIBC_MAX'])
    jump = 'b' if env['GLIBC_MAX_ARCH'] == 'aarch64' else 'jmp'

    def action(target, source, env):
        wanted = _undefined([str(node) for node in source])
        lines = ['\t.text']
        wraps = []
        for library in ('libc.so.6', 'libm.so.6'):
            for name, (default, versions) in sorted(_symbols(_library(env, library)).items()):
                if name not in wanted or default is None or default <= limit:
                    continue
                older = [ver for ver in versions if ver <= limit]
                if not older:
                    continue        # check() names it
                old = '.'.join(str(part) for part in max(older))
                lines += ['\t.globl __wrap_%s' % name,
                          '\t.type __wrap_%s, %%function' % name,
                          '__wrap_%s:' % name,
                          '\t%s __glibc_max_%s%s' % (jump, name, '' if jump == 'b' else '@PLT'),
                          '\t.symver __glibc_max_%s, %s@GLIBC_%s' % (name, name, old)]
                wraps.append('%s (GLIBC_%s)' % (name, old))
        lines.append('\t.section .note.GNU-stack,"",%progbits')
        with open(asm, 'w') as stream:
            stream.write('\n'.join(lines) + '\n')
        subprocess.run([env.subst('$CC'), '-c', '-fpie', asm, '-o', obj], check=True)
        with open(response, 'w') as stream:
            stream.write(obj + '\n')
            for wrap in wraps:
                stream.write('-Wl,--wrap=%s\n' % wrap.split()[0])
        if wraps:
            print('glibc_max %s: %s' % (env['GLIBC_MAX'], ', '.join(wraps)))
    return action


def check(target, source, env):
    """The post-action of the program: no symbol newer than glibc_max."""
    limit = version(env['GLIBC_MAX'])
    path = str(target[0])
    out = subprocess.run(['objdump', '-T', path], check=True, capture_output=True, text=True).stdout
    newer = []
    for line in out.splitlines():
        match = _LINE.search(line)
        if match is not None and '*UND*' in line and version(match.group(2)) > limit:
            newer.append('%s (GLIBC_%s)' % (match.group(3), match.group(2)))
    if newer:
        os.remove(path)
        raise Exception('glibc_max %s: %s asks for a newer glibc: %s'
                        % (env['GLIBC_MAX'], os.path.basename(path), ', '.join(sorted(newer))))
