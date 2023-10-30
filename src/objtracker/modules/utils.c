#include <Python.h>

#include "objtracker.h"
#include "frameobject.h"

#define ASUTF8(obj) PyUnicode_AsUTF8(obj)
#define PyAsUTF8(obj) PyUnicode_AsUTF8(PyObject_Repr(obj))

void Print_Stack(PyFrameObject *frame);

enum Color {
  BLACK = 30,
  RED = 31,
  GREEN = 32,
  YELLOW = 33,
  BLUE = 34,
  MAGENTA = 35,
  CYAN = 36,
  WHITE = 37,
  DEFAULT = 39
};

void Print_Py(PyObject *o)
{
  PyObject *repr = PyObject_Repr(o);
  if (repr != NULL) {
    printf("%s\n", ASUTF8(repr));
  }
  Py_XDECREF(repr);
}

void Print_Obj(PyObject *o)
{
  // print of cpython object
  const char *filename = "print.txt";
  FILE *fp;
  char buf[2048];
  errno_t err = 0;

  // write data in file:
  errno = fopen_s(&fp, filename, "w");
  PyObject_Print(o, fp, Py_PRINT_RAW);
  fclose(fp);

  // read data from file:
  err = fopen_s(&fp, filename, "r");
  fgets(buf, 2048, fp);
  printf("object -> %s\n", buf);
  fclose(fp);
}

void Printer(const char *content, enum Color color)
{
  printf("\033[0;%dm%s\033[0m\n", color, content);
}

void Print_Trace_Info(struct ObjectNode *node)
{
  Printer("====== Trace Triggered ======", RED);
  Printer("Call Stack (most recent call last):", RED);
  // if (log_stack > 0)
  //   Print_Stack(frame);
  Printer(ASUTF8(PyUnicode_FromFormat("lineno: %d -> %s (call: %s)", node->lineno, ASUTF8(node->filename), ASUTF8(node->name))), GREEN);
  Printer("<", CYAN);

  PyObject *func_args = PyDict_GetItemString(node->args, "func_args");

  Py_ssize_t pos = 0;
  PyObject *key = NULL;
  PyObject *value = NULL;
  while (PyDict_Next(func_args, &pos, &key, &value)) {
    if (PyLong_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: int = %s", "    ", ASUTF8(key), PyAsUTF8(value))), WHITE);
    } else if (PyFloat_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: float = %s", "    ", ASUTF8(key), PyAsUTF8(value))), WHITE);
    } else if (PyUnicode_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: str = %s", "    ", ASUTF8(key), PyAsUTF8(value))), WHITE);
    } else if (PyList_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: list = %s", "    ", ASUTF8(key), PyAsUTF8(value))), BLUE);
    } else if (PyTuple_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: tuple = %s", "    ", ASUTF8(key), PyAsUTF8(value))), BLUE);
    } else if (PyDict_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: dict = %s", "    ", ASUTF8(key), PyAsUTF8(value))), BLUE);
    } else if (PySet_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: set = %s", "    ", ASUTF8(key), PyAsUTF8(value))), BLUE);
    } else if (PyBytes_Check(value)) {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: bytes = ...", "    ", ASUTF8(key))), BLUE);
    } else {
      Printer(ASUTF8(PyUnicode_FromFormat("%s%s: %s = %s", "    ", ASUTF8(key), PyAsUTF8(PyObject_Type(value)), ASUTF8(PyObject_Repr(value)))), MAGENTA);
    }
  }

  Printer(">", CYAN);
  Printer("", DEFAULT);
}

void Print_Stack(PyFrameObject *frame)
{
  PyFrameObject *f_back = frame->f_back;
  while (f_back->f_back) {
    f_back = f_back->f_back;
  }
  
  PyObject *format_stack_method = PyObject_GetAttrString(traceback_module, "format_stack");
  if (!format_stack_method) {
    perror("Failed to get format stack method");
    exit(-1);
  }

  PyObject *tuple = PyTuple_New(2);
  PyTuple_SetItem(tuple, 0, (PyObject *) f_back);
  PyTuple_SetItem(tuple, 1, PyLong_FromLong(3));
  Py_INCREF(f_back);

  PyObject *stacks = PyObject_CallObject(format_stack_method, tuple);
  if (!stacks) {
    perror("Failed to call format stack method");
    exit(-1);
  } else {
    Py_DECREF(tuple);
    Py_DECREF(format_stack_method);
  }

  PyObject *stack = NULL;
  for (int index = 0; index < PyList_GET_SIZE(stacks); index++) {
    stack = PyList_GetItem(stacks, index);
    Printer(ASUTF8(stack), RED);
  }
  Py_XDECREF(stack);
}

void Ignore_Builtin_Trace(PyObject *filename, int lineno)
{
  Printer("====== Trace Failed ======", RED);
  Printer("Warning: Built-in methods cannot be tracked, Because it is implemented by CPython.", YELLOW);
  Printer(ASUTF8(PyUnicode_Concat(PyUnicode_FromFormat("lineno: %d -> ", lineno), filename)), GREEN);
  Printer("", DEFAULT);
}
