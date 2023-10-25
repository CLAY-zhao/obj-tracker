from typing import Any


def replace_backslashes(obj: Any) -> Any:
  if isinstance(obj, dict):
    return {k: replace_backslashes(v) for k, v in obj.items()}
  elif isinstance(obj, list):
    return [replace_backslashes(item) for item in obj]
  elif isinstance(obj, str):
    return obj.replace("\\", "/")
  else:
    return obj
