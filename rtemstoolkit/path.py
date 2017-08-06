#
# RTEMS Tools Project (http://www.rtems.org/)
# Copyright 2010-2016 Chris Johns (chrisj@rtems.org)
# All rights reserved.
#
# This file is part of the RTEMS Tools package in 'rtems-tools'.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

#
# Manage paths locally. Internally the path is in Unix or shell format and
# we convert to the native format when performing operations at the Python
# level. This allows macro expansion to work.
#

from __future__ import print_function

import glob
import os
import shutil
import string

#
# Support to handle use in a package and as a unit test.
# If there is a better way to let us know.
#
try:
    from . import error
    from . import log
except (ValueError, SystemError):
    import error
    import log

windows = os.name == 'nt'

def host(path):
    if path is not None:
        while '//' in path:
            path = path.replace('//', '/')
        if windows and len(path) > 2:
            if path[0] == '/' and path[2] == '/' and \
                    (path[1] in string.ascii_lowercase or \
                         path[1] in string.ascii_uppercase):
                path = ('%s:%s' % (path[1], path[2:])).replace('/', '\\')
    return path

def shell(path):
    if path is not None:
        if windows and len(path) > 1 and path[1] == ':':
            path = ('/%s%s' % (path[0], path[2:])).replace('\\', '/')
        while '//' in path:
            path = path.replace('//', '/')
    return path

def basename(path):
    return shell(os.path.basename(path))

def dirname(path):
    return shell(os.path.dirname(path))

def join(path, *args):
    path = shell(path)
    for arg in args:
        if len(path):
            path += '/' + shell(arg)
        else:
            path = shell(arg)
    return shell(path)

def abspath(path):
    return shell(os.path.abspath(host(path)))

def splitext(path):
    root, ext = os.path.splitext(host(path))
    return shell(root), ext

def exists(paths):
    if type(paths) == list:
        results = []
        for p in paths:
            results += [os.path.exists(host(p))]
        return results
    return os.path.exists(host(paths))

def isdir(path):
    return os.path.isdir(host(path))

def isfile(path):
    return os.path.isfile(host(path))

def isabspath(path):
    return path[0] == '/'

def iswritable(path):
    return os.access(host(path), os.W_OK)

def ispathwritable(path):
    path = host(path)
    while len(path) != 0:
        if os.path.exists(path):
            return iswritable(path)
        path = os.path.dirname(path)
    return False

def mkdir(path):
    path = host(path)
    if exists(path):
        if not isdir(path):
            raise error.general('path exists and is not a directory: %s' % (path))
    else:
        if windows:
            try:
                os.makedirs(host(path))
            except IOError:
                raise error.general('cannot make directory: %s' % (path))
            except OSError:
                raise error.general('cannot make directory: %s' % (path))
            except WindowsError:
                raise error.general('cannot make directory: %s' % (path))
        else:
            try:
                os.makedirs(host(path))
            except IOError:
                raise error.general('cannot make directory: %s' % (path))
            except OSError:
                raise error.general('cannot make directory: %s' % (path))

def removeall(path):

    def _onerror(function, path, excinfo):
        print('removeall error: (%s) %s' % (excinfo, path))

    path = host(path)
    shutil.rmtree(path, onerror = _onerror)
    return

def expand(name, paths):
    l = []
    for p in paths:
        l += [join(p, name)]
    return l

def collect_files(path_):
    #
    # Convert to shell paths and return shell paths.
    #
    # @fixme should this use a passed in set of defaults and not
    #        not the initial set of values ?
    #
    path_ = shell(path_)
    if '*' in path_ or '?' in path_:
        dir = dirname(path_)
        base = basename(path_)
        if len(base) == 0:
            base = '*'
        files = []
        for p in dir.split(':'):
            hostdir = host(p)
            for f in glob.glob(os.path.join(hostdir, base)):
                files += [host(f)]
    else:
        files = [host(path_)]
    return sorted(files)

def copy_tree(src, dst):
    hsrc = host(src)
    hdst = host(dst)

    if os.path.exists(src):
        names = os.listdir(src)
    else:
        name = []

    if not os.path.isdir(dst):
        os.makedirs(dst)

    for name in names:
        srcname = os.path.join(src, name)
        dstname = os.path.join(dst, name)
        try:
            if os.path.islink(srcname):
                linkto = os.readlink(srcname)
                if os.path.exists(dstname):
                    if os.path.islink(dstname):
                        dstlinkto = os.readlink(dstname)
                        if linkto != dstlinkto:
                            log.warning('copying tree: update of link does not match: %s -> %s' % \
                                            (dstname, dstlinkto))
                            os.remove(dstname)
                    else:
                        log.warning('copying tree: destination is not a link: %s' % \
                                        (dstname))
                        os.remove(dstname)
                else:
                    os.symlink(linkto, dstname)
            elif os.path.isdir(srcname):
                copy_tree(srcname, dstname)
            else:
                shutil.copy2(srcname, dstname)
        except shutil.Error as err:
            raise error.general('copying tree: %s -> %s: %s' % (src, dst, str(err)))
        except EnvironmentError as why:
            raise error.general('copying tree: %s -> %s: %s' % (srcname, dstname, str(why)))
    try:
        shutil.copystat(src, dst)
    except OSError as why:
        ok = False
        if windows:
            if WindowsError is not None and isinstance(why, WindowsError):
                ok = True
        if not ok:
            raise error.general('copying tree: %s -> %s: %s' % (src, dst, str(why)))

if __name__ == '__main__':
    print(host('/a/b/c/d-e-f'))
    print(host('//a/b//c/d-e-f'))
    print(shell('/w/x/y/z'))
    print(basename('/as/sd/df/fg/me.txt'))
    print(dirname('/as/sd/df/fg/me.txt'))
    print(join('/d', 'g', '/tyty/fgfg'))
    windows = True
    print(host('/a/b/c/d-e-f'))
    print(host('//a/b//c/d-e-f'))
    print(shell('/w/x/y/z'))
    print(shell('w:/x/y/z'))
    print(basename('x:/sd/df/fg/me.txt'))
    print(dirname('x:/sd/df/fg/me.txt'))
    print(join('s:/d/', '/g', '/tyty/fgfg'))
