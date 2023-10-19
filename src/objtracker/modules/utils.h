#ifndef __OBJTRACKER_UTIL_H__
#define __OBJTRACKER_UTIL_H__

#include <Python.h>

void Print_Py(PyObject *o);
void Print_Obj(PyObject *o);
void Print_Trace_Info(PyFrameObject *frame, PyObject *arginfo, PyObject *objname, PyObject *filename, int lineno, int log_stack);
void Ignore_Builtin_Trace(PyObject *filename, int lineno);

#endif
