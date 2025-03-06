#!/usr/bin/env python
# -*- coding: utf-8 -*-
import os
import sys
import subprocess

filetypes = [
  ".cpp",
  ".cxx",
  ".cc",
  ".c",
  ".h",
  ".hpp",
  ".hxx",
  ".fx",
  ".hlsl",
  ]

def check_need_convert(filename):
    for filetype in filetypes:
        if filename.lower().endswith(filetype):
            return True
    return False

def check_need_ignore(dir):
    filename = os.path.join(dir, 'ignore.clang-format')
    return os.path.isfile(filename)

def format_dir(root_dir):
    if os.path.exists(root_dir) == False:
        print("[error] dir:",root_dir,"do not exit")
        return
    print("work in", root_dir)
    format_count = 0
    if check_need_ignore(root_dir):
        return
    for root, dirs, files in os.walk(root_dir, True):
        [dirs.remove(dir) for dir in list(dirs) if check_need_ignore(os.path.join(root, dir))]
        for f in files:
            if check_need_convert(f):
                filename = os.path.join(root, f)
                print('Format file: ' + filename)
                subprocess.check_call(args=['clang-format', '-i', filename])
                format_count += 1
    print('Total format {count} files.'.format(count = format_count))
    return format_count

if __name__ == '__main__':
    if len(sys.argv) == 1:
        dir = os.path.join(os.getcwd(), 'src')
    else:
        dir = sys.argv[1]
    format_dir(dir)
    sys.exit(0)
