import json
import os
from typing import Optional

from .utils import replace_backslashes
from .color_util import COLOR
from .tracer import _Tracker


class Tracker(_Tracker):
  
  def __init__(
      self,
      log_func_args: bool = False,
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

  def __enter__(self) -> "Tracker":
    self.start()
    return self
  
  def __exit__(self, exc_type, exc_value, trace) -> None:
    self.stop()
    self.save()

  def save(self, output_file: Optional[str] = None) -> None:
    if output_file is None:
      output_file = self.output_file
    
    enabled = False
    
    if isinstance(output_file, str):
      output_file = os.path.abspath(output_file)
      if not os.path.isdir(os.path.dirname(output_file)):
        os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    if self.enable:
      enabled = True
      self.stop()
    
    self.dump(output_file)
    
    with open(output_file, "r") as file:
      data = file.read()
      data = data.replace("\\", "/")
      
    data = replace_backslashes(json.loads(data))
    
    with open(output_file, "w") as updated:
      json.dump(data, updated, ensure_ascii=False, indent=4)
      
    if enabled:
      self.start()
