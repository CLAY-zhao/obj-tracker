import sys
import inspect
from types import MethodType

import objtracker.tracker as objtracker


class Tracker(object):
  
  def __init__(
    self,
    output_file: str = None
  ) -> None:
    self.initialized = False
    self.output_file = output_file
    self._objtracker = objtracker.ObjTracker()
    self.initialized = True
    
  def trace(self, callable_obj, log_stack=False) -> None:
    frame = inspect.currentframe().f_back
    if isinstance(callable_obj, MethodType):
      cls = callable_obj.__self__.__class__
    else:
      cls = None
    self._objtracker.ftrace(callable_obj, frame=frame, cls=cls, log_stack=log_stack)
    del frame
    
  def install(self, func="tracker") -> None:
    import builtins
    setattr(builtins, func, self)
  
  def uninstall(self, func="tracker") -> None:
    import builtins
    if hasattr(builtins, func):
      delattr(builtins, func)
      

if sys.platform == "win32":
  try:
    # https://stackoverflow.com/questions/36760127/...
    # how-to-use-the-new-support-for-ansi-escape-sequences-in-the-windows-10-console
    from ctypes import windll
    kernel32 = windll.kernel32
    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
  except Exception:
    pass
