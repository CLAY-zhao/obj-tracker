from typing import Optional, Union

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
    return self.__log_func_args
  
  @log_func_args.setter
  def log_func_args(self, log_func_args: bool) -> None:
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
    
  def dump(self, filename: str) -> None:
    self._objtracker.dump(filename)
