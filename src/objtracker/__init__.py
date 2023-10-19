__version__ = "1.0.0"

from .objtrace import Tracker
from .main import main
from .decorator import bound_trace

__all__ = [
  "__version__",
  "main",
  "objtrace",
  "bound_trace"
]

objtrace = Tracker()
