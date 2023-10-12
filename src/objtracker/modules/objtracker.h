#ifndef __OBJTRACKER_H__
#define __OBJTRACKER_H__

#include <Python.h>

#define PY_FUNCTION 1
#define PY_METHOD 2

struct ObjectNode
{
  struct ObjectNode *next;
  PyObject* obj;
  int type;
};

typedef struct
{
  PyObject_HEAD
  int trace_total;
  struct ObjectNode* trackernode;
  PyObject* output_file;
} ObjTrackerObject;

#endif