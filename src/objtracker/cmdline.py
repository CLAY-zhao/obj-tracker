import cmd
import os
import sys
import traceback
from typing import Optional
from types import FrameType

from objprint import objprint

# line_prefix = ': '    # Use this to get the old situation back
line_prefix = '\n-> '   # Probably a better default


class Cmd(cmd.Cmd):
  
  prompt = "(Pot) "
  
  def __init__(
      self,
      objtrace = None, 
      curframe: Optional[FrameType] = None,
      *args,
      **kwargs
  ) -> None:
    super().__init__(*args, **kwargs)
    self.wait_for_main = True
    self.objtrace = objtrace
    if curframe is None:
      self.curframe = sys._getframe().f_back
    else:
      self.curframe = curframe
    self.trackback = None
    self.maxsourceline = 10
    self.fncache = {}
    
  def canonic(self, filename: str) -> str:
    if filename == "<" + filename[1:-1] + ">":
      return filename
    canonic = self.fncache.get(filename)
    if not canonic:
      canonic = os.path.abspath(filename)
      canonic = os.path.normcase(canonic)
      self.fncache[filename] = canonic
    return canonic
  
  def print_stack_trace(self):
    import linecache, reprlib
    frame = self.curframe
    lineno = frame.f_lineno
    filename = self.canonic(frame.f_code.co_filename)
    s = '%s(%r)' % (filename, lineno)
    if frame.f_code.co_name:
      s += frame.f_code.co_name
    else:
      s += "<lambda>"
    s += "()"
    if "__return__" in frame.f_locals:
      rv = frame.f_locals["__return__"]
      s += "->"
      s += reprlib.repr(rv)
    line = linecache.getline(filename, lineno, frame.f_globals)
    if line:
      s += line_prefix + line.strip()
    self.message(s)

  def setup(self, curframe, trackback):
    self.curframe = curframe
    self.trackback = trackback
  
  def do_where(self, arg):
    """Print a stack trace,"""
    self.print_stack_trace()
  do_w = do_where
  
  def _getval(self, arg):
    try:
      return eval(arg, self.curframe.f_globals, self.curframe.f_locals)
    except:
      self._error_exc()
      raise
  
  def _msg_val_func(self, arg, func):
    try:
      val = self._getval(arg)
    except:
      return
    try:
      func(val)
    except:
      self._error_exc()
  
  def do_pp(self, arg):
    self._msg_val_func(arg, objprint)
    
  def do_step(self, arg):
    return 1
  do_s = do_step
    
  def do_continue(self, arg):
    self.objtrace.breakpoint = 0
    return 1
  do_c = do_cont = do_continue

  def do_quit(self, arg):
    # Quit from the debugger. The program being executed is aborted.
    self.objtrace.breakpoint = 2
    return 1
  do_q = do_exit = do_quit
  
  def displayhook(self, obj):
    if obj is not None:
      self.message(repr(obj))
  
  def default(self, line: str) -> None:
    if line[:1] == "!":
      line = line[1:]
    f_locals = self.curframe.f_locals
    f_globals = self.curframe.f_globals
    try:
      code = compile(line + "\n", "<stdin>", "single")
      save_stdout = sys.stdout
      save_stdin = sys.stdin
      save_displayhook = sys.displayhook
      try:
        sys.stdin = self.stdin
        sys.stdout = self.stdout
        sys.displayhook = self.displayhook
        exec(code, f_globals, f_locals)
      finally:
        sys.stdout = save_stdout
        sys.stdin = save_stdin
        sys.displayhook = save_displayhook
    except:
      self._error_exc()

  def set_trace(self):
    while True:
      try:
        if self.wait_for_main:
          self.print_stack_trace()
          self.cmdloop()
          break
      except KeyboardInterrupt:
        self.message("--KeyboardInterrupt--")
  
  @property
  def wait_for_main(self):
    return self.__wait_for_main
  
  @wait_for_main.setter
  def wait_for_main(self, flag):
    if not isinstance(flag, bool):
      flag = False
    self.__wait_for_main = flag
    
  def message(self, msg: str):
    print(">>> " + msg, file=self.stdout)
    
  def error(self, msg: str):
    print("***", msg, file=self.stdout)
    
  def _error_exc(self):
    exc_info = sys.exc_info()[:2]
    self.error(traceback.format_exception_only(*exc_info)[-1].strip())


if __name__ == "__main__":
  command = Cmd(wait_for_main=True)
  
  class Hoppy:
    def __init__(self) -> None:
      self.like = "games"
      self.like_num = 10
  
  class Person:
    def __init__(self) -> None:
      self.name = "John"
      self.hoppy = Hoppy()
      
      
  person = Person()
  
  a = 1
  b = 2
  
  command.set_trace()
