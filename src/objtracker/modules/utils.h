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
void PrintWarning(PyObject *msg);
void PrintError(const char* msg);

inline int startswith(const char* target, const char* prefix)
{
  while (*target != 0 && *prefix != 0) {
#if _WIN32
    // Windows path has double slashes and case-insensitive
    if (*prefix == '\\' && prefix[-1] == '\\') {
      prefix++;
    }
    if (*target == '\\' && target[-1] == '\\') {
      target++;
    }
    if (*target != *prefix && *target != *prefix - ('a'-'A') && *target != *prefix + ('a'-'A')) {
      return 0;
    }
#else
    if (*target != *prefix) {
      return 0;
    }
#endif
    target++;
    prefix++;
  }

  return (*prefix) == 0;
}

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
  return ((double)t.tv_sec * 1e9 + t.tv_nsec);
#endif
}

#endif
