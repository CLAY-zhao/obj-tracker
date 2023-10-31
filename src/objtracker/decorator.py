import functools
from typing import Optional, Union, Callable, List, Tuple, Any


def trace_hook(
    method: Optional[Callable] = None, alias: str = None,
    when_type_trigger: Union[List, Tuple] = None,
    when_value_trigger: Union[List, Tuple] = None
  ) -> Callable:
  
  def inner(func: Callable) -> Callable:
    
    @functools.wraps(func)
    def wrapper(*args, **kwargs) -> Any:
      pass

    return wrapper
  
  if method:
    return inner(method)
  return inner
