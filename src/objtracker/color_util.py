# Licensed under the Apache License: http://www.apache.org/licenses/LICENSE-2.0
# For details: https://github.com/gaogaotiantian/objprint/blob/master/NOTICE.txt

import sys

__all__ = ["warning_print"]

class COLOR:
    BLACK = '\033[30m'
    RED = '\033[31m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    BLUE = '\033[34m'
    MAGENTA = '\033[35m'
    CYAN = '\033[36m'
    WHITE = '\033[37m'
    DEFAULT = '\033[39m'

if sys.platform == "win32":
  try:
    # https://stackoverflow.com/questions/36760127/...
    # how-to-use-the-new-support-for-ansi-escape-sequences-in-the-windows-10-console
    from ctypes import windll
    kernel32 = windll.kernel32
    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
  except Exception:
    pass


def warning_print(message):
  print(f"\033[0;{COLOR.YELLOW} {message} \033[0m")
