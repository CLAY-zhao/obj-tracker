from typing import Optional, Union, Callable, List, Tuple

import objtracker.tracker as objtracker

from . import __version__


class _Tracker(object):
  
  def __init__(
      self,
      log_func_args: bool = False,
      output_file: Optional[str] = None
    ) -> None:
    self.initialized = False
    self.enable = False
    self.parsed = False
    self._objtracker = objtracker.ObjTracker()
    self.log_func_args = log_func_args
    self.output_file = output_file
    self.initialized = True

  @property
  def log_func_args(self) -> int:
    """Whether to print trace information"""
    return self.__log_func_args
  
  @log_func_args.setter
  def log_func_args(self, log_func_args: bool) -> None:
    """Whether to print trace information"""
    if isinstance(log_func_args, bool):
      self.__log_func_args = log_func_args
    else:
      raise ValueError(f"log_func_args needs to be True or False, not {log_func_args}")
    self.config()
  
  @property
  def output_file(self) -> str:
    return self.__output_file
  
  @output_file.setter
  def output_file(self, output_file: Optional[str]) -> None:
    if output_file is None:
      self.__output_file = None
    elif isinstance(output_file, str):
      self.__output_file = output_file
    else:
      raise ValueError("output_file has to be a string")
    self.config()

  def config(self) -> None:
    if not self.initialized:
      return
    
    config = {
      "log_func_args": self.log_func_args,
      "output_file": self.output_file
    }
    
    self._objtracker.config(**config)

  def start(self) -> None:
    self.enable = True
    self.parsed = False
    self.config()
    self._objtracker.start()
    
  def stop(self) -> None:
    self.enable = False
    self._objtracker.stop()
    
  def pause(self) -> None:
    if self.enable:
      self._objtracker.pause()
  
  def resume(self) -> None:
    if self.enable:
      self._objtracker.resume()
    
  def dump(self, filename: str) -> None:
    self._objtracker.dump(filename)

  def trace_hook(
      self, callback: Optional[Callable] = None, alias: str = None,
      when_type_trigger: Union[Tuple, List] = None,
      when_value_trigger: Union[Tuple, List] = None,
      terminate: bool = False
    ):
    self._objtracker.addtracehook(
      callback=callback,
      alias=alias,
      when_type_trigger=when_type_trigger,
      when_value_trigger=when_value_trigger,
      terminate=terminate
    )
