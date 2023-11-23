import os
from typing import Optional, Union, Callable, List, Tuple, Sequence, Any

import objtracker.tracker as objtracker

from . import __version__
from .cmdline import Cmd


class _Tracker(object):
  
  def __init__(
      self,
      log_func_args: bool = False,
      breakpoint: bool = False,
      output_file: Optional[str] = None,
      exclude_files: Optional[List] = None,
    ) -> None:
    self.initialized = False
    self.enable = False
    self.parsed = False
    self.log_func_args = log_func_args
    self.breakpoint = breakpoint
    self.output_file = output_file
    self.exclude_files = exclude_files
    self.pdb = Cmd(self)
    self._objtracker = objtracker.ObjTracker(self.pdb)
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
  def breakpoint(self):
    return self.__breakpoint
  
  @breakpoint.setter
  def breakpoint(self, breakpoint: bool) -> bool:
    if isinstance(breakpoint, (bool, int)):
      self.__breakpoint = 1 if breakpoint is True else breakpoint
    else:
      raise ValueError(f"breakpoint needs to be True or False, not {breakpoint}")
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

  @property
  def exclude_files(self) -> Sequence[str]:
    return self.__exclude_files
  
  @exclude_files.setter
  def exclude_files(self, exclude_files: Sequence[str]) -> None:
    if exclude_files is None:
      import functools
      from objtracker import (objtrace, tracer, decorator)
      self.__exclude_files = [objtrace.__file__, tracer.__file__, decorator.__file__, functools.__file__]
    elif isinstance(exclude_files, list):
      if exclude_files:
        self.__exclude_files = exclude_files[:] + [os.path.abspath(f) for f in exclude_files if not f.startswith("/")]
      else:
        self.__exclude_files = None
    else:
      raise ValueError("exclude_files has to be a list")
    self.config()

  def config(self) -> None:
    if not self.initialized:
      return
    
    config = {
      "log_func_args": self.log_func_args,
      "breakpoint": self.breakpoint,
      "output_file": self.output_file,
      "exclude_files": self.exclude_files
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

  def return_trace(
      self, c_id: int, on_raise: bool = False, iterative_compare: bool = False,
      return_values: Union[List, Tuple] = None
    ):
    self._objtracker.addreturntrace(
      id=c_id, on_raise=on_raise, iterative_compare=iterative_compare,
      return_values=return_values
    )
