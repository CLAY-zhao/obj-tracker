import atexit
import argparse
import os
import sys
import signal
import types
from typing import Optional, Any, Dict, List

from . import __version__
from .objtrace import Tracker


class TraceUI(object):
  
  def __init__(self) -> None:
    self.objtrace: Optional[Tracker] = None
    self.parser: argparse.ArgumentParser = self.create_parser()
    self.verbose: int = 1
    self.output_file: str = "result.json"
    self.options: argparse.Namespace = argparse.Namespace()
    self.cwd: str = os.getcwd()
    
  def create_parser(self) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python -m objtrace")
    parser.add_argument("--version", action="store_true", default=False,
                        help="show version of objtrace")
    parser.add_argument("--output_file", "-o", nargs="?", default=None,
                        help="output file path. End with .json")
    parser.add_argument("--log_func_args", action="store_true", default=False,
                        help="log all function arguments, this will introduce large overhead")
    parser.add_argument("--open", action="store_true", default=False,
                        help="open report in browser after saving")
    
    return parser
    
  def parse(self, argv: List[str]):
    # If -- or --run exists, all the commands after --/--run are the commands we need to run
        # We need to filter those out, they might conflict with our arguments
    idx: Optional[int] = None
    if "--" in argv[1:]:
        idx = argv.index("--")
    elif "--run" in argv[1:]:
        idx = argv.index("--run")
    
    if idx is not None:
      options, command = self.parser.parse_args(argv[1:idx]), argv[idx + 1:]
      self.args = argv[1:idx]
    else:
      options, command = self.parser.parse_known_args(argv[1:])
      self.args = [elem for elem in argv[1:] if elem not in command]
      
    self.options, self.command = options, command
    self.init_kwargs = {
      # "verbose": self.verbose,
      "output_file": self.output_file,
      "log_func_args": options.log_func_args
    }
    
    return True, None
  
  def search_file(self, file_name: str) -> Optional[str]:
    if os.path.isfile(file_name):
      return file_name
    
    # search file in $PATH
    if "PATH" in os.environ:
      if sys.platform in ("linux", "linux2", "darwin"):
        path_sep = ":"
      elif sys.platform in ("win32",):
        path_sep = ";"
      else:  # pragma: no cover
        return None
      
      for dir_name in os.environ["PATH"].split(path_sep):
        candidate = os.path.join(dir_name, file_name)
        if os.path.isfile(candidate):
          return candidate
    
    return None
  
  def show_version(self):
    print(__version__)
    return True, None

  def run_command(self):
    command = self.command
    options = self.options
    file_name = command[0]
    search_result = self.search_file(file_name)
    if not search_result:
      return False, f"No such file as {file_name}"
    file_name = search_result
    with open(file_name, 'rb') as f:
      code_string = f.read()
    
    main_mod = types.ModuleType("__main__")
    setattr(main_mod, "__file__", os.path.abspath(file_name))
    setattr(main_mod, "__builtins__", globals()["__builtins__"])
    
    sys.modules["__main__"] = main_mod
    code = compile(code_string, os.path.abspath(file_name), "exec")
    sys.path.insert(0, os.path.dirname(file_name))
    sys.argv = command[:]
    return self.run_code(code, main_mod.__dict__)
    
  def run_code(self, code: Any, global_dict: Dict[str, Any]) -> Any:
    options = self.options
    self.parent_pid = os.getpid()
    
    objtrace = Tracker(**self.init_kwargs)
    self.objtrace = objtrace
    
    def term_handler(signalnum, frame):
      sys.exit(0)
      
    signal.signal(signal.SIGTERM, term_handler)
    
    objtrace.start()
    
    exec(code, global_dict)
    
    objtrace.stop()
    
    return True, None
    
  def run(self):
    # if self.options.version:
    #   return self.show_version()
    # elif self.command:
    return self.run_command()


def main():
  ui = TraceUI()
  success, err_msg = ui.parse(sys.argv)
  if not success:
    print(err_msg)
    sys.exit(1)
  try:
    success, err_msg = ui.run()
    if not success:
      print(err_msg)
      sys.exit(1)
  finally:
    atexit._run_exitfuncs()
