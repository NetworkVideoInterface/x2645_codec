#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sys
import subprocess

class colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'

if __name__ == '__main__':
    print('- Check merge commits')
    proc = subprocess.Popen(["git", "log", "--merges", "-n", "1", "FETCH_HEAD..HEAD"], stdout=subprocess.PIPE, encoding='UTF-8')
    proc.wait()
    out = proc.stdout.read()
    if out != '':
        print('\n' + out + '\n')
        print(f'{colors.FAIL}Has merge commit! This is not allowed.{colors.ENDC}')
        sys.exit(1)
    print(f'{colors.OKGREEN}Check merge commits OK.{colors.ENDC}')
    sys.exit(0)
