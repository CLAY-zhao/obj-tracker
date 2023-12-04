import functools
from typing import Optional, Union, Callable, List, Tuple, Any

from .objtrace import Tracker, get_objtrace


def ignore_trace(method: Optional[Callable] = None, objtrace: Optional[Tracker] = None) -> Callable:
  
  def inner(func: Callable) -> Callable:
    
    @functools.wraps(func)
    def ignore_wrapper(*args, **kwargs) -> Any:
      tracer = objtrace
      if tracer is None:
        tracer = get_objtrace()
        if not tracer:
          raise NameError("ignore_trace only works with global objtrace")
      tracer.pause()
      ret = func(*args, **kwargs)
      tracer.resume()
      return ret

    return ignore_wrapper
  
  if method:
    return inner(method)
  return inner


def trace_and_save(
    method: Optional[Callable] = None, output_file: str = "./result.json", 
    **objtrace_kwargs
  ) -> Callable:
  
  def inner(func: Callable) -> Callable:
    
    @functools.wraps(func)
    def wrapper(*args, **kwargs) -> Any:
      objtrace = Tracker(**objtrace_kwargs)
      objtrace.start()
      ret = func(*args, **kwargs)
      objtrace.stop()
      objtrace.save(output_file)
      return ret

    return wrapper
  
  if method:
    return inner(method)
  return inner


def trace_return(
    method: Optional[Callable] = None, on_raise: bool = False,
    iterative_compare: bool = False, return_values: Union[List, Tuple] = None,
    objtrace: Optional[Tracker] = None
  ):
  
  def inner(func: Optional[Callable]) -> Callable:
    tracer = objtrace
    if tracer is None:
      tracer = get_objtrace()
      if not tracer:
        raise NameError("trace_return only works with global objtrace")
    tracer.add_return_trace(func, on_raise, iterative_compare, return_values)

    @functools.wraps(func)
    def wrapper(*args, **kwargs):
      return func(*args, **kwargs)

    return wrapper
  
  if method:
    return inner(method)
  return inner


def register_hook(
    method: Optional[Callable] = None,
    when_type_trigger: Union[List, Tuple] = None,
    when_value_trigger: Union[List, Tuple] = None,
    alias: str = None,
    terminate: bool = False,
    objtrace: Optional[Tracker] = None
  ):
  
  def inner(func: Callable) -> Callable:
    tracer = objtrace
    if tracer is None:
      tracer = get_objtrace()
      if not tracer:
        raise NameError("register_hook only works with global objtrace")
    tracer.add_hook(func, when_type_trigger, when_value_trigger, alias, terminate)
    
    @functools.wraps(func)
    def wrapper(*args, **kwargs) -> Any:
      return tracer
    
    return wrapper
  
  if method:
    return inner(method)
  return inner
