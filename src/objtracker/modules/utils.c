#include <Python.h>

#include "objtracker.h"
#include "frameobject.h"

#define ASUTF8(obj) PyUnicode_AsUTF8(obj)

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
  printf("%s\n", ASUTF8(repr));
  Py_DECREF(repr);
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

void Print_Trace_Info(PyFrameObject *frame, PyObject *arginfo, PyObject *filename, int lineno, int log_stack)
{
  PyObject *varnames = PyObject_GetAttrString(arginfo, "args");
  PyObject *argname = PyObject_GetAttrString(arginfo, "varargs");
  PyObject *kwname = PyObject_GetAttrString(arginfo, "keywords");
  PyObject *locals = PyObject_GetAttrString(arginfo, "locals");

  Printer("====== Trace Triggered ======", RED);
  Printer("Call Stack (most recent call last):", RED);
  if (log_stack > 0)
    Print_Stack(frame);
  Printer(ASUTF8(PyUnicode_Concat(PyUnicode_FromFormat("lineno: %d -> ", lineno), filename)), GREEN);

  // print attrs
  Py_ssize_t size = PyList_GET_SIZE(varnames);
  PyObject *name = NULL;
  PyObject *value = NULL;

  if (size > 0 || argname != Py_None || kwname != Py_None) {
    Printer("<", MAGENTA);
    for (int index = 0; index < size; index++) {
      name = PyList_GetItem(varnames, index);
      value = PyDict_GetItem(locals, name);
      Py_INCREF(name);
      Py_INCREF(value);
      if (PyNumber_Check(value)) {
        Printer(ASUTF8(PyUnicode_FromFormat("%s%s: [int | float] = %s", "    ", ASUTF8(name), ASUTF8(PyObject_Repr(value)))), WHITE);
      } else if (PyUnicode_Check(value)) {
        Printer(ASUTF8(PyUnicode_FromFormat("%s%s: str = %s", "    ", ASUTF8(name), ASUTF8(PyObject_Repr(value)))), BLUE);
      } else {
        Printer(ASUTF8(PyUnicode_FromFormat("%s%s: any = %s", "    ", ASUTF8(name), ASUTF8(PyObject_Repr(value)))), CYAN);
      }
      Py_DECREF(name);
      Py_DECREF(value);
    }
    if (argname != Py_None) {
      value = PyDict_GetItem(locals, argname);
      Py_INCREF(value);
      Printer(ASUTF8(PyUnicode_FromFormat("%s*%s: %s", "    ", ASUTF8(argname), ASUTF8(PyObject_Repr(value)))), YELLOW);
      Py_DECREF(value);
    }
    if (kwname != Py_None) {
      Py_INCREF(value);
      value = PyDict_GetItem(locals, kwname);
      Printer(ASUTF8(PyUnicode_FromFormat("%s**%s: %s", "    ", ASUTF8(kwname), ASUTF8(PyObject_Repr(value)))), YELLOW);
      Py_DECREF(value);
    }
    Printer(">", MAGENTA);
  }
  Printer("", DEFAULT);

  Py_DECREF(varnames);
  Py_DECREF(argname);
  Py_DECREF(kwname);
  Py_DECREF(locals);
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
