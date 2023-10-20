#ifndef __OBJTRACKER_H__
#define __OBJTRACKER_H__

#include <Python.h>

#define PY_FUNCTION 1
#define PY_METHOD 2
#define PY_CLASS 3
#define PY_MODULE 4

struct ObjectNode
{
  struct ObjectNode *next;
  int lineno;
  PyObject *filename;
  PyObject *name;
  PyObject *args;
  int len;
};

typedef struct
{
  PyObject_HEAD
  int trace_total;
  int collecting;
  int log_func_args;
  struct ObjectNode* trackernode;
  char* output_file;
} ObjTrackerObject;

extern PyObject* traceback_module;

#endif