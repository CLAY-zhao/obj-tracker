from .objtrace import Tracker
from .decorator import bound_trace

__version__ = "1.0.0"

__all__ = [
  "__version__",
  "objtrace",
  "bound_trace"
]

objtrace = Tracker()
