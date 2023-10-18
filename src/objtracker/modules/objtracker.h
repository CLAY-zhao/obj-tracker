#ifndef __OBJTRACKER_H__
#define __OBJTRACKER_H__

#include <Python.h>

#define PY_FUNCTION 1
#define PY_METHOD 2
#define PY_CLASS 3
#define PY_MODULE 4

PyObject* traceback_module;

struct ObjectNode
{
  struct ObjectNode *next;
  PyObject* obj;
  PyObject* origin;
  int type;
  int log_stack;
};

typedef struct
{
  PyObject_HEAD
  int trace_total;
  int collecting;
  struct ObjectNode* trackernode;
  PyObject* output_file;
} ObjTrackerObject;

#endif