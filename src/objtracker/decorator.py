import functools
from typing import Any, Callable, Optional

from .objtrace import Tracker


def bound_trace(
    method: Optional[Callable] = None,
    triggered_callback: Optional[Callable] = None,
    log_stack=False,
    **objtrace_kwargs
  ):
  
  def inner(func: Callable) -> Callable:

    if hasattr(method, "__mro__"):
      objtrace = Tracker(**objtrace_kwargs)
      objtrace.trace(method, log_stack=log_stack)
      return method
    else:
      @functools.wraps(func)
      def wrapper(*args, **kwargs) -> Any:
        objtrace = Tracker(**objtrace_kwargs)
        objtrace.trace(func, log_stack=log_stack)
        ret = func(*args, **kwargs)
        return ret
      return wrapper
  
  if method:
    return inner(method)
  return inner
