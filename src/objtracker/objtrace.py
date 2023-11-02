import builtins
import json
import os
from typing import Optional, Union, Callable, List, Tuple

from .utils import replace_backslashes
from .color_util import COLOR
from .tracer import _Tracker


class Tracker(_Tracker):
  
  def __init__(
      self,
      log_func_args: bool = False,
      output_file: str = "result.json",
      register_global: bool = True
  ) -> None:
    super().__init__(
      log_func_args=log_func_args,
      output_file=output_file
    )
    if register_global:
      self.register_global()
    
  def start(self):
    """Start trace run"""
    if not self.enable:
      _Tracker.start(self)
  
  def stop(self):
    """Stop trace run"""
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
    
  def register_global(self) -> None:
    builtins.__dict__["__obj_tracker__"] = self

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
  
  def add_hook(
      self, callback: Optional[Callable] = None,
      when_type_trigger: Union[Tuple, List] = None,
      when_value_trigger: Union[Tuple, List] = None,
      alias: str = None,
      terminate: bool = False
  ):
    """
    You can track the trigger through the add_hook method. Every time you add a hook method,
    the hook callback method will be called one by one every time a call is triggered in the
    Python code, and if the return value is returned in the hook method,
    the return value will be will be passed as the first parameter of the next hook call.

      >>> tracer = Tracker()
      >>> tracer.add_hook(hook_func)

    You can also set the conditions under which the hook method is triggered. For example,
    you can limit the hook call to be triggered only when it matches a certain type or a
    specified value. (Type precedence is higher than value precedence)

      # When a trace is triggered and the parameter type received is int or float. The typing module type is not supported for the time being.
      >>> tracer.add_hook(hook_func, when_type_trigger=[int, float])

      # The parameter value received when a trace is triggered is 1 or the "chiu" string
      >>> tracer.add_hook(hook_func, when_value_trigger=[1, "Chiu"])

    It is not recommended to set type triggering and value triggering at the same time.

    Support chain calls
      >>> tracer.add_hook(hook_func1).add_hook(hook_func2).add_hook(hook_func3)

    In some cases, you may only want to stop the program after a certain hook is triggered. In this case, you can specify "terminate" as True.
    
    Notice: It should be noted that the trigger type supports bytes type but does not support bytes value!
    """
    if callback is None:
      return self
    
    if alias is None:
      try:
        if not hasattr(callback, "__class__"):
          alias = callback.__name__
        else:
          alias = callback.__class__.__name__
      except AttributeError:
        alias = repr(callback)

    if when_type_trigger is not None:
      if not isinstance(when_type_trigger, (list, tuple)):
        return self
      when_type_trigger = tuple(when_type_trigger)
    
    if when_value_trigger is not None:
      if not isinstance(when_value_trigger, (list, tuple)):
        return self
      when_value_trigger = tuple(when_value_trigger)

    self.trace_hook(
      callback=callback,
      alias=alias,
      when_type_trigger=when_type_trigger,
      when_value_trigger=when_value_trigger,
      terminate=terminate
    )
    return self

  def add_hook_number(self, callback: Optional[Callable] = None, alias: str = None, terminate: bool = False):
    """for numeric types"""
    return self.add_hook(callback=callback, when_type_trigger=[int, float], alias=alias, terminate=terminate)

  def add_hook_string(self, callback: Optional[Callable] = None, alias: str = None, terminate: bool = False):
    """for string types"""
    return self.add_hook(callback=callback, when_type_trigger=[str], alias=alias, terminate=terminate)
  
  def add_hook_list(self, callback: Optional[Callable] = None, alias: str = None, terminate: bool = False):
    """for list types"""
    return self.add_hook(callback=callback, when_type_trigger=[list], alias=alias, terminate=terminate)
  
  def add_hook_tuple(self, callback: Optional[Callable] = None, alias: str = None, terminate: bool = False):
    """for tuple types"""
    return self.add_hook(callback=callback, when_type_trigger=[tuple], alias=alias, terminate=terminate)
  
  def add_hook_dict(self, callback: Optional[Callable] = None, alias: str = None, terminate: bool = False):
    """for dict types"""
    return self.add_hook(callback=callback, when_type_trigger=[dict], alias=alias, terminate=terminate)
  
  def add_hook_set(self, callback: Optional[Callable] = None, alias: str = None, terminate: bool = False):
    """for set types"""
    return self.add_hook(callback=callback, when_type_trigger=[set], alias=alias, terminate=terminate)
  
  def add_hook_any(self, callback: Optional[Callable] = None, alias: str = None, terminate: bool = False):
    """for any types"""
    return self.add_hook(callback=callback, when_type_trigger=[object], alias=alias, terminate=terminate)


def get_objtrace() -> Optional[Tracker]:
  return builtins.__dict__.get("__obj_tracker__", None)
