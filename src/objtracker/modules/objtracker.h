#ifndef __OBJTRACKER_H__
#define __OBJTRACKER_H__

#include <Python.h>

struct ObjectNode
{
  struct ObjectNode *next;
  PyObject* name;
  PyObject* tail;
  PyObject* filename;
  PyObject* codestring;
  int lineno;
};

typedef struct
{
  PyObject_HEAD
  int trace_total;
  struct ObjectNode* trackernode;
  PyObject* output_file;
} ObjTrackerObject;

#endif