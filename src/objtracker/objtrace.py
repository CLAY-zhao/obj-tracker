from .color_util import COLOR

from .tracer import _Tracker


class Tracker(_Tracker):
  
  def __init__(
      self,
      log_func_args: int = 1,
      output_file: str = "result.json"
  ) -> None:
    super().__init__(
      log_func_args=log_func_args,
      output_file=output_file
    )
    
  def start(self):
    if not self.enable:
      _Tracker.start(self)
  
  def stop(self):
    if self.enable:
      _Tracker.stop(self)
    
  def install(self, func="tracker") -> None:
    import builtins
    setattr(builtins, func, self)
  
  def uninstall(self, func="tracker") -> None:
    import builtins
    if hasattr(builtins, func):
      delattr(builtins, func)
