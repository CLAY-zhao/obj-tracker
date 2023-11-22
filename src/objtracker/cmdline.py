import cmd
import inspect
import sys
import traceback
from typing import Optional
from types import FrameType


class Cmd(cmd.Cmd):
  
  prompt = "(Orc) "
  
  def __init__(
      self,
      wait_for_main: bool = False,
      curframe: Optional[FrameType] = None,
      *args,
      **kwargs
  ) -> None:
    super().__init__(*args, **kwargs)
    self.wait_for_main = wait_for_main
    if curframe is None:
      self.curframe = inspect.currentframe().f_back
    else:
      self.curframe = curframe
    self.trackback = None
    self.codeline = "pass"

  def setup(self, curframe, trackback):
    self.curframe = curframe
    self.trackback = trackback
    
  def do_recall(self, arg):
    self.default(self.codeline)
    
  do_cc = do_recall
  
  def do_where(self, arg):
    pass
  
  do_w = do_where
    
  def do_quit(self, arg):
    # Quit from the debugger. The program being executed is aborted.
    return 1
  
  do_q = do_exit = do_quit
  
  def displayhook(self, obj):
    if obj is not None:
      self.message(repr(obj))
  
  def default(self, line: str) -> None:
    if line[:1] == "!":
      line = line[1:]
      
    self.codeline = line
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
      self.codeline = "pass"
      self._error_exc()

  def set_trace(self):
    while True:
      try:
        if self.wait_for_main:
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
  
  class Person:
    def __init__(self) -> None:
      self.name = "John"
      
  person = Person()
  
  a = 1
  b = 2
  
  command.set_trace()
