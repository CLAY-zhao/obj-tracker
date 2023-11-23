import pdb

from objprint import objprint


class Pob(pdb.Pdb):
  
  def __init__(
      self,
      objtrace = None,
      *args,
      **kwargs
  ) -> None:
    super().__init__(*args, **kwargs)
    self.objtrace = objtrace
    self.prompt = "(Pot) "

  def _msg_val_func(self, arg, func):
    try:
        val = self._getval(arg)
    except:
        return  # _getval() has displayed the error
    try:
        func(val)
    except:
        self._error_exc()
  
  def do_pp(self, arg):
    self._msg_val_func(arg, objprint)
