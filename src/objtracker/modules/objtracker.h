#ifndef __OBJTRACKER_H__
#define __OBJTRACKER_H__

#include <Python.h>
#if _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

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
  unsigned long tid;
  double ts;
  double prev_ts;
  double dur;
  int len;
};

struct MetadataNode {
  unsigned long tid;
  PyObject *name;
  struct MetadataNode *next;
};

typedef struct
{
  PyObject_HEAD
  int trace_total;
  int collecting;
  long fix_pid;
  int log_func_args;
  struct ObjectNode* trackernode;
  char* output_file;
#ifdef Py_NOGIL
  PyMutex mutex;
#endif
  struct MetadataNode* metadata;
} ObjTrackerObject;

extern PyObject* inspect_module;
extern PyObject* traceback_module;

#endif