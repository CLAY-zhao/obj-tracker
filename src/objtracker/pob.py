import bdb
import pdb

from .color_util import warning_print

from objprint import objprint

class Pob(pdb.Pdb):

  def __init__(self, log_locals_trace=True, *args, **kwargs) -> None:
    super().__init__(*args, **kwargs)
    self.log_locals_trace = log_locals_trace
    self.prompt = "(Pob) "
    
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
    return super().do_step(arg)
  do_s = do_step
  
  def trace_dispatch(self, frame, event, arg):
    try:
      return super().trace_dispatch(frame, event, arg)
    except bdb.BdbQuit:
      warning_print("--- exit pob ---")
      exit(-1)
