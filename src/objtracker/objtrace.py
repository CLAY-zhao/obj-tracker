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
    
  def trace(self, callable_obj) -> None:
    frame = inspect.currentframe().f_back
    if isinstance(callable_obj, MethodType):
      pass
    self._objtracker.ftrace(callable_obj, frame=frame)
    del frame
    
  def install(self, func="tracker") -> None:
    import builtins
    setattr(builtins, func, self)
  
  def uninstall(self, func="tracker") -> None:
    import builtins
    if hasattr(builtins, func):
      delattr(builtins, func)
