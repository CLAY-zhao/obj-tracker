#ifndef __OBJTRACKER_UTIL_H__
#define __OBJTRACKER_UTIL_H__

#include <Python.h>

#if _WIN32
#include <windows.h>
extern LARGE_INTEGER qpc_freq;
#endif

void Print_Py(PyObject *o);
void Print_Obj(PyObject *o);
void Print_Trace_Info(struct ObjectNode *node);
void Ignore_Builtin_Trace(PyObject *filename, int lineno);

inline double get_system_ts(void)
{
#if _WIN32
  LARGE_INTEGER counter = {0};
  QueryPerformanceCounter(&counter);
  double current_ts = (double) counter.QuadPart;
  current_ts *= 1000000000LL;
  current_ts /= qpc_freq.QuadPart;
  return current_ts;
#else
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return ((double)t.tv_sec * 1e9 + tv_nsec);
#endif
}

#endif
