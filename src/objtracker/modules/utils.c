#include <Python.h>

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
  printf("%s\n", PyUnicode_AsUTF8(repr));
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

void Print_Trace_Info(PyObject *arginfo, PyObject *filename, int lineno)
{
  PyObject *varnames = PyObject_GetAttrString(arginfo, "args");
  PyObject *argname = PyObject_GetAttrString(arginfo, "varargs");
  PyObject *kwname = PyObject_GetAttrString(arginfo, "keywords");
  PyObject *locals = PyObject_GetAttrString(arginfo, "locals");
  
  /*
    ArgInfo(
      args=['a', 'b', 'c'], varargs=None, keywords=None,
      locals={'a': 1, 'b': 2, 'c': 99}
    )
  */

  Printer("====== Trace Triggered ======", RED);
  Printer(PyUnicode_AsUTF8(PyUnicode_Concat(PyUnicode_FromFormat("lineno: %d -> ", lineno), filename)), GREEN);
  Printer(PyUnicode_AsUTF8(PyObject_Repr(varnames)), BLUE);
  Printer("Call Stack (most recent call last):", RED);
  Printer("", DEFAULT);

  Py_DECREF(varnames);
  Py_DECREF(argname);
  Py_DECREF(kwname);
  Py_DECREF(locals);
}
