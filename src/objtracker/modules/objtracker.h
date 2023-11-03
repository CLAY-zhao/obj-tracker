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

struct TraceInfoCallback {
  PyObject* callback;
  char* alias;
  PyObject* when_type_trigger;
  PyObject* when_value_trigger;
  int terminate; // Used to mark whether to end the run
  struct TraceInfoCallback *next;
};

struct ReturnTrace {
  long long id;
  int on_raise; // Whether to throw an exception to terminate the program when the comparison fails
  int iterative_compare;
  int subscript;
  int size;
  PyObject* return_values;
  struct ReturnTrace *next;
};

typedef struct
{
  PyObject_HEAD
  int trace_total;
  int collecting;
  long fix_pid;
  int log_code_detail;
  int log_func_args;
  struct ObjectNode* trackernode;
  struct TraceInfoCallback* tracecallback;
  struct ReturnTrace* returntrace;
  PyObject* exclude_files;
  char* output_file;
#ifdef Py_NOGIL
  PyMutex mutex;
#endif
  struct MetadataNode* metadata;
} ObjTrackerObject;

extern PyObject* inspect_module;
extern PyObject* traceback_module;

#endif