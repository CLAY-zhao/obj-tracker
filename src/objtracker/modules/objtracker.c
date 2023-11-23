#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#include <time.h>
#if _WIN32
#include <windows.h>
#elif __APPLE
#include <pthread.h>
#else
#include <pthread.h>
#endif
#include "pythoncapi_compat.h"
#include "objtracker.h"
#include "utils.h"

#define EQ(left, right) PyObject_RichCompareBool(left, right, Py_EQ)
#define NE(left, right) PyObject_RichCompareBool(left, right, Py_NE)
#define GET_STR(obj) PyUnicode_AsUTF8(PyObject_Repr(obj))

// Function declarations

int objtracker_tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg);
static PyObject* objtracker_start(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_stop(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_pause(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_resume(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_addtracehook(PyObject *obj, PyObject *args, PyObject *kwds);
static PyObject* objtracker_addreturntrace(ObjTrackerObject *obj, PyObject *args, PyObject *kwds);
static PyObject* objtracker_config(ObjTrackerObject *self, PyObject *args, PyObject *kwds);
static PyObject* objtracker_dump(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_createthreadinfo(ObjTrackerObject *self);
static void trigger_trace_hook(ObjTrackerObject *self);
static void log_func_args(struct ObjectNode *node, PyFrameObject *frame);
static void trace_return_value(struct ReturnTrace *returntrace, PyFrameObject *frame, PyObject *arg);

ObjTrackerObject* curr_tracker = NULL;
PyObject* inspect_module = NULL;
PyObject* traceback_module = NULL;
PyObject* thread_module = NULL;
PyObject* multiprocessing_module = NULL;

#if _WIN32
LARGE_INTEGER qpc_freq;
#endif

#ifdef Py_NOGIL
#define OBJTRACKER_THREAD_PROTECT_START(self) Py_BEGIN_CRITICAL_SECTION(&self->mutex)
#define OBJTRACKER_THREAD_PROTECT_END(self) Py_END_CRITICAL_SECTION
#else
#define OBJTRACKER_THREAD_PROTECT_START(self)
#define OBJTRACKER_THREAD_PROTECT_END(self)
#endif

static PyMethodDef ObjTrakcer_methods[] = {
  {"start", (PyCFunction)objtracker_start, METH_VARARGS, "start tracker"},
  {"stop", (PyCFunction)objtracker_stop, METH_VARARGS, "stop tracker"},
  {"pause", (PyCFunction)objtracker_pause, METH_VARARGS, "pause tracker"},
  {"resume", (PyCFunction)objtracker_resume, METH_VARARGS, "resume tracker"},
  {"addtracehook", (PyCFunction)objtracker_addtracehook, METH_VARARGS | METH_KEYWORDS, "add trace hook"},
  {"addreturntrace", (PyCFunction)objtracker_addreturntrace, METH_VARARGS | METH_KEYWORDS, "add return trace"},
  {"config", (PyCFunction)objtracker_config, METH_VARARGS | METH_KEYWORDS, "config tracker"},
  {"dump", (PyCFunction)objtracker_dump, METH_VARARGS, "dump tracker"},
  {NULL, NULL, 0, NULL}
};

// ============================================================================
// Python interface
// ============================================================================

static double get_ts(struct ObjectNode *node)
{
  double current_ts = get_system_ts();
  if (current_ts <= node->prev_ts) {
    current_ts = node->prev_ts + 20;
  }
  node->prev_ts = current_ts;
  return current_ts;
}

static void trigger_trace_hook(ObjTrackerObject *self)
{
  struct TraceInfoCallback *tracehook = self->tracecallback;
  PyObject* result = NULL;
  PyObject* value = NULL;
  Py_ssize_t pos = 0;
  PyObject* func_args = PyDict_GetItemString(self->trackernode->args, "func_args");
  while (tracehook) {
    if (tracehook->when_type_trigger) {
      while (PyDict_Next(func_args, &pos, NULL, &value)) {
        if (PyObject_IsInstance(value, tracehook->when_type_trigger)) {
          goto hookup;
          break;
        }
      }
      // if (suppore) {
      //   break;
      // }
      if (tracehook->next) {
        tracehook = tracehook->next;
        pos = 0; // reload
        continue;
      } else {
        goto cleanup;
      }
    }

    if (tracehook->when_value_trigger) {
      pos = 0; // reload
      PyObject *iter = PyObject_GetIter(tracehook->when_value_trigger);
      PyObject *next = NULL;
      while ((next = PyIter_Next(iter)) != NULL) {
        while (PyDict_Next(func_args, &pos, NULL, &value)) {
          if (EQ(value, next)) {
            goto hookup;
            break;
          }
        }
      }

      Py_XDECREF(iter);
      Py_XDECREF(next);

      if (tracehook->next) {
        tracehook = tracehook->next;
        pos = 0;
        continue;
      } else {
        goto cleanup;
      }
    }

hookup:
    if (result == NULL || result == Py_None) {
      PyObject* tuple = PyTuple_New(1);
      PyTuple_SetItem(tuple, 0, self->trackernode->args);
      Py_INCREF(self->trackernode->args);
      result = PyObject_CallObject(tracehook->callback, tuple);
    } else {
      PyObject* tuple = PyTuple_New(2);
      PyTuple_SetItem(tuple, 0, result);
      PyTuple_SetItem(tuple, 1, self->trackernode->args);
      Py_INCREF(self->trackernode->args);
      result = PyObject_CallObject(tracehook->callback, tuple);
    }

    if (PyErr_Occurred()) {
      PyObject *exc_type, *exc_value, *exc_tb;
      if (PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_Fetch(&exc_type, &exc_value, &exc_tb);
        printf("\n\033[0;31mTypeError: %s, (Please check if the return value is set in the previous hook, but the next hook does not accept enough parameters?)\033[0m\n",
              PyUnicode_AsUTF8(exc_value));
        exit(-1);
      }
    }

    if (tracehook->terminate >= 1) {
      printf("\n\033[0;33mThe program has been terminated. Please set \"terminate\" to False to resume.\033[0m\n");
      exit(-1);
    }
    tracehook = tracehook->next;
  }

cleanup:
  Py_XDECREF(func_args);
  Py_XDECREF(result);
}

static void log_func_args(struct ObjectNode *node, PyFrameObject *frame)
{
  PyCodeObject *code = NULL;
  PyFrameObject *f_back = NULL;
  PyObject *co_filename = NULL;
  PyObject *co_name = NULL;
  int lineno = 0;

  code = PyFrame_GetCode(frame);
  f_back = PyFrame_GetBack(frame);

  co_filename = code->co_filename;
  co_name = code->co_name;
  lineno = frame->f_lineno;

  PyObject *getargvalues_method = PyObject_GetAttrString(inspect_module, "getargvalues");
  if (!getargvalues_method) {
    perror("Failed to access inspect.getargvalues()");
    exit(-1);
  }
  PyObject *args = PyTuple_New(1);
  PyTuple_SetItem(args, 0, (PyObject *) frame);
  Py_INCREF(frame);
  PyObject *arginfo = NULL;
  arginfo = PyObject_CallObject(getargvalues_method, args);
  if (!arginfo) {
    perror("Failed to call inspect.getargvalues()");
    exit(-1);
  }
  Py_DECREF(args);
  Py_DECREF(getargvalues_method);

  if (PyObject_Repr(arginfo)) {
    // search attr
    PyObject *names = PyObject_GetAttrString(arginfo, "args");
    PyObject *argname = PyObject_GetAttrString(arginfo, "varargs");
    PyObject *kwdname = PyObject_GetAttrString(arginfo, "keywords");
    PyObject *locals = PyObject_GetAttrString(arginfo, "locals");
    PyObject *func_arg_dict = PyDict_New();
    Py_ssize_t name_length = PyList_GET_SIZE(names);

    int idx = 0;
    if (!node->args) {
      node->args = PyDict_New();
    }

    node->lineno = lineno;
    node->filename = co_filename;
    node->name = co_name;

    while (idx < name_length) {
      PyObject *name = PyList_GET_ITEM(names, idx);
      PyObject *value = PyDict_GetItem(locals, name);
      if (!value) {
        value = PyUnicode_FromString("Not Displayable");
        PyErr_Clear();
      }
      if (PyObject_Size(value) > 20) {
        PyDict_SetItem(func_arg_dict, name, PyUnicode_FromString("..."));
      } else if (PyBytes_Check(value)) {
        PyDict_SetItem(func_arg_dict, name, PyUnicode_FromString("..."));
      } else if (PyCode_Check(value)) {
        PyDict_SetItem(func_arg_dict, name, PyUnicode_FromString("..."));
      } else {
        PyDict_SetItem(func_arg_dict, name, value);
      }
      Py_DECREF(value);
      idx++;
    }

    // if (argname != Py_None) {
    //   PyObject *value = PyDict_GetItem(locals, argname);
    //   if (!value) {
    //     value = PyUnicode_FromString("Not Displayable");
    //     PyErr_Clear();
    //   }
    //   PyDict_SetItemString(func_arg_dict, "*args", value);
    //   Py_DECREF(value);
    //   idx++;
    // }

    // if (kwdname != Py_None) {
    //   PyObject *value = PyDict_GetItem(locals, kwdname);
    //   if (!value) {
    //     value = PyUnicode_FromString("Not Displayable");
    //     PyErr_Clear();
    //   }
    //   PyDict_SetItemString(func_arg_dict, "**kwargs", value);
    //   Py_DECREF(value);
    //   idx++;
    // }

    PyDict_SetItemString(node->args, "func_args", func_arg_dict);
    node->len = idx;
    Py_DECREF(func_arg_dict);

    Py_XDECREF(code);
    Py_XDECREF(names);
  }
}

static void trace_return_value(struct ReturnTrace *returntrace, PyFrameObject *frame, PyObject *arg)
{
  PyObject* code = PyFrame_GetCode(frame);
  PyObject* id = PyLong_FromVoidPtr(code);
  PyObject* func = frame->f_code->co_name;
  PyObject* result = NULL;

  while (returntrace) {
    if (returntrace->id != PyLong_AsLongLong(id)) {
      goto next;
    }

    if (!returntrace->iterative_compare) {
      result = PyTuple_GetItem(returntrace->return_values, returntrace->subscript);
      if (NE(arg, result)) {
        if (returntrace->on_raise) {
          PrintError(PyUnicode_FromFormat("Error: (call: %s)\n>>> %s != %s <- return\n", GET_STR(func), GET_STR(result), GET_STR(arg)));
          exit(-1);
        } else {
          PrintWarning(PyUnicode_FromFormat("Warning: (call: %s)\n>>> %s != %s <- return\n", GET_STR(func), GET_STR(result), GET_STR(arg)));
        }
      }
    } else {
      if (returntrace->subscript + 1 <= returntrace->size) {
        result = PyTuple_GetItem(returntrace->return_values, returntrace->subscript);
        if (NE(arg, result)) {
          if (returntrace->on_raise) {
            PrintError(PyUnicode_FromFormat("Error: (call: %s)\n>>> %s != %s <- return\n", GET_STR(func), GET_STR(result), GET_STR(arg)));
            exit(-1);
          } else {
            PrintWarning(PyUnicode_FromFormat("Warning: (call: %s)\n>>> %s != %s <- return\n", GET_STR(func), GET_STR(result), GET_STR(arg)));
          }
        }
        returntrace->subscript++;
      } else {
        PrintWarning(PyUnicode_FromFormat("The verification range has been exceeded. The current maximum range is: %d", returntrace->size));
      }
    }

    Py_XDECREF(result);

next:
    returntrace = returntrace->next;
  }
}

static int
objtracker_tracefuncdisabled(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg)
{
  PyEval_SetTrace(objtracker_tracefunc, obj);
  return objtracker_tracefunc(obj, frame, what, arg);
}

static PyObject*
objtracker_start(ObjTrackerObject *self, PyObject *args)
{
  if (curr_tracker) {
    printf("Warning! Overwrite tracker! You should not have two ObjTracker recording at the same time\n");
  } else {
    curr_tracker = self;
  }

  self->collecting = 1;
  PyEval_SetTrace(objtracker_tracefunc, (PyObject *) self);

  Py_RETURN_NONE;
}

static PyObject*
objtracker_stop(ObjTrackerObject *self, PyObject *args)
{
  if (self) {
    self->collecting = 0;
    // PyMem_FREE(self->trackernode);
  }
  curr_tracker = NULL;
  PyEval_SetTrace(NULL, NULL);

  Py_RETURN_NONE;
}

static PyObject*
objtracker_pause(ObjTrackerObject *self, PyObject *args)
{
  if (self->collecting) {
    PyGILState_STATE state = PyGILState_Ensure();
    PyEval_SetTrace(NULL, NULL);
    PyGILState_Release(state);
  }
  Py_RETURN_NONE;
}

static PyObject*
objtracker_resume(ObjTrackerObject *self, PyObject *args)
{
  if (self->collecting) {
    PyGILState_STATE state = PyGILState_Ensure();
    PyEval_SetTrace(objtracker_tracefunc, (PyObject*)self);
    PyGILState_Release(state);
  }
  Py_RETURN_NONE;
}

int
objtracker_tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg)
{
  ObjTrackerObject *self = (ObjTrackerObject *)obj;

  if (!self->collecting) {
    PyEval_SetTrace(objtracker_tracefuncdisabled, obj);
    return 0;
  }

  if (self->breakpoint & 0x2) {
    exit(-1);
  }

  if (what == PyTrace_CALL || what == PyTrace_RETURN || 
        what == PyTrace_C_CALL || what == PyTrace_C_RETURN || what == PyTrace_C_EXCEPTION) {
    int is_call = (what == PyTrace_CALL || what == PyTrace_C_CALL);
    int is_return = (what == PyTrace_RETURN || what == PyTrace_C_RETURN || what == PyTrace_C_EXCEPTION);

    // Check include/exclude files
    if (self->exclude_files) {
      PyObject* files = NULL;
      int record = 0;
      files = self->exclude_files;
      Py_ssize_t length = PyList_GET_SIZE(files);
      PyObject* name = frame->f_code->co_filename;
      for (int i = 0; i < length; i++) {
        PyObject* f = PyList_GET_ITEM(files, i);
        if (startswith(PyUnicode_AsUTF8(name), PyUnicode_AsUTF8(f))) {
          record++;
          break;
        }
      }
      if (record != 0) {
        return 0;
      }
    }

    if (is_call) {
      if (!self->trackernode) {
        self->trackernode = (struct ObjectNode *) PyMem_Calloc(1, sizeof(struct ObjectNode));
        log_func_args(self->trackernode, frame);
      } else {
        struct ObjectNode *tmp = self->trackernode;
        self->trackernode = (struct ObjectNode *) PyMem_Calloc(1, sizeof(struct ObjectNode));
        self->trackernode->next = tmp;
        log_func_args(self->trackernode, frame);
      }
      self->trackernode->ts = get_ts(self->trackernode);
      objtracker_createthreadinfo(self);

      if (self->log_func_args) {
        if (self->trackernode->args) {
          Print_Trace_Info(self->trackernode);
        }
      }

      if (self->breakpoint) {
        PyObject* set_trace = PyObject_GetAttrString(self->pdb, "set_trace");
        PyObject_CallObject(set_trace, NULL);
      }
      
      if (self->tracecallback) {
        trigger_trace_hook(self);
      }
    } else if (is_return) {
      if (self->trackernode) {
        double dur = get_ts(self->trackernode) - self->trackernode->ts;
        self->trackernode->dur = dur;
      }
      if (self->returntrace) {
        trace_return_value(self->returntrace, frame, arg);
      }
    }
  } else if (what == PyTrace_LINE) {
    // Check include/exclude files
    if (self->exclude_files) {
      PyObject* files = NULL;
      int record = 0;
      files = self->exclude_files;
      Py_ssize_t length = PyList_GET_SIZE(files);
      PyObject* name = frame->f_code->co_filename;
      for (int i = 0; i < length; i++) {
        PyObject* f = PyList_GET_ITEM(files, i);
        if (startswith(PyUnicode_AsUTF8(name), PyUnicode_AsUTF8(f))) {
          record++;
          break;
        }
      }
      if (record != 0) {
        return 0;
      }
    }

    if (self->breakpoint) {
      PyObject* set_trace = PyObject_GetAttrString(self->pdb, "set_trace");
      PyObject_CallObject(set_trace, NULL);
    }
  }

  return 0;
}

static PyObject*
objtracker_addtracehook(PyObject *obj, PyObject *args, PyObject *kwds)
{
  static char* kwlist[] = {"callback", "alias", "when_type_trigger", "when_value_trigger", "terminate", NULL};
  PyObject *kw_callback = NULL;
  char* kw_alias = NULL;
  PyObject *kw_when_type_trigger = NULL;
  PyObject *kw_when_value_trigger = NULL;
  int kw_terminate = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OsOOi", kwlist,
        &kw_callback,
        &kw_alias,
        &kw_when_type_trigger,
        &kw_when_value_trigger,
        &kw_terminate)) {
          return NULL;
        }

  ObjTrackerObject *self = (ObjTrackerObject *)obj;
  if (!self->tracecallback) {
    self->tracecallback = (struct TraceInfoCallback *) PyMem_Calloc(1, sizeof(struct TraceInfoCallback));
  } else {
    struct TraceInfoCallback *tmp = self->tracecallback;
    self->tracecallback = (struct TraceInfoCallback *) PyMem_Calloc(1, sizeof(struct TraceInfoCallback));
    self->tracecallback->next = tmp;
  }

  if (PyCallable_Check(kw_callback)) {
    self->tracecallback->callback = kw_callback;
  }

  if (kw_alias) {
    self->tracecallback->alias = kw_alias;
  }

  if (PyList_Check(kw_when_type_trigger) || PyTuple_Check(kw_when_type_trigger)) {
    self->tracecallback->when_type_trigger = kw_when_type_trigger;
    Py_INCREF(self->tracecallback->when_type_trigger);
  } else {
    self->tracecallback->when_type_trigger = NULL;
  }

  if (PyList_Check(kw_when_value_trigger) || PyTuple_Check(kw_when_value_trigger)) {
    self->tracecallback->when_value_trigger = kw_when_value_trigger;
    Py_INCREF(self->tracecallback->when_value_trigger);
  } else {
    self->tracecallback->when_value_trigger = NULL;
  }

  if (kw_terminate >= 0) {
    self->tracecallback->terminate = kw_terminate;
  } else {
    self->tracecallback->terminate = 0;
  }
  
  Py_RETURN_NONE;
}

static PyObject*
objtracker_addreturntrace(ObjTrackerObject *obj, PyObject *args, PyObject *kwds)
{
  static char* kwlist[] = {"id", "on_raise", "iterative_compare", "return_values",
          NULL};
  long long kw_id = 0;
  int kw_on_raise = 0;
  int kw_iterative_compare = 0;
  int kw_subscript = 0;
  PyObject* kw_return_values = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|LiiO", kwlist,
        &kw_id,
        &kw_on_raise,
        &kw_iterative_compare,
        &kw_return_values)) {
      return NULL;
  }
  
  ObjTrackerObject *self = (ObjTrackerObject *)obj;
  if (!self->returntrace) {
    self->returntrace = (struct ReturnTrace *) PyMem_Calloc(1, sizeof(struct ReturnTrace));
  } else {
    struct ReturnTrace *tmp = self->returntrace;
    self->returntrace = (struct ReturnTrace *) PyMem_Calloc(1, sizeof(struct ReturnTrace));
    self->returntrace->next = tmp;
  }

  if (kw_id >= 0) {
    self->returntrace->id = kw_id;
  }

  if (kw_on_raise >= 0) {
    self->returntrace->on_raise = kw_on_raise;
  }

  if (kw_iterative_compare >= 0) {
    self->returntrace->iterative_compare = kw_iterative_compare;
  }

  if (PyTuple_Check(kw_return_values)) {
    self->returntrace->size = PyTuple_GET_SIZE(kw_return_values);
    self->returntrace->return_values = kw_return_values;
    Py_INCREF(self->returntrace->return_values);
  }

  self->returntrace->subscript = 0;

  Py_RETURN_NONE;
}

static PyObject*
objtracker_config(ObjTrackerObject *self, PyObject *args, PyObject *kwds)
{
  static char* kwlist[] = {"log_func_args", "breakpoint", "output_file", 
          "exclude_files", NULL};
  int kw_log_func_args = 0;
  int kw_breakpoint = 0;
  char* kw_output_file = NULL;
  PyObject* kw_exclude_files = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iisO", kwlist,
        &kw_log_func_args,
        &kw_breakpoint,
        &kw_output_file,
        &kw_exclude_files)) {
      return NULL;
  }

  if (kw_log_func_args >= 0) {
    self->log_func_args = kw_log_func_args;
  }

  if (kw_breakpoint >= 0) {
    self->breakpoint = kw_breakpoint;
  }

  if (kw_output_file) {
    if (self->output_file) {
      PyMem_FREE(self->output_file);
    }
    self->output_file = PyMem_Calloc((strlen(kw_output_file) + 1), sizeof(char));
    if (!self->output_file) {
      printf("Out of memory!\n");
      exit(1);
    }
    strcpy(self->output_file, kw_output_file);
  }

  if (kw_exclude_files && kw_exclude_files != Py_None) {
    if (self->exclude_files) {
      Py_DECREF(self->exclude_files);
    }
    self->exclude_files = kw_exclude_files;
    Py_INCREF(self->exclude_files);
  }

  Py_RETURN_NONE;
}

static PyObject*
objtracker_dump(ObjTrackerObject *self, PyObject *args)
{
  const char* filename = NULL;
  FILE* fptr = NULL;
  if (!PyArg_ParseTuple(args, "s", &filename)) {
    PyErr_SetString(PyExc_ValueError, "Missing required file name");
    Py_RETURN_NONE;
  }
  fptr = fopen(filename, "w");
  if (!fptr) {
    PyErr_Format(PyExc_ValueError, "Cant't open file %s to write", filename);
    Py_RETURN_NONE;
  }

  fprintf(fptr, "{\"traceEvents\": [");

  OBJTRACKER_THREAD_PROTECT_START(self);
  struct ObjectNode *node = self->trackernode;
  unsigned long pid = 0;
  struct MetadataNode* metadata = NULL;

  if (self->fix_pid > 0) {
    pid = self->fix_pid;
  } else {
#if _WIN32
    pid = GetCurrentProcessId();
#else
    pid = getpid();
#endif
  }

  // Process Name
  {
    PyObject* current_process_method = PyObject_GetAttrString(multiprocessing_module, "current_process");
    if (!current_process_method) {
      perror("Failed to access multiprocessing.current_process()");
      exit(-1);
    }
    PyObject* current_process = PyObject_CallObject(current_process_method, NULL);
    if (!current_process) {
      perror("Failed to access multiprocessing.current_process()");
      exit(-1);
    }
    PyObject* process_name = PyObject_GetAttrString(current_process, "name");

    Py_DECREF(current_process_method);
    Py_DECREF(current_process);
    // fprintf(fptr, "{\"ph\":\"M\",\"pid\":%lu,\"tid\":%lu,\"name\":\"process_name\",\"args\":{\"name\":\"%s\"}},",
    //         pid, pid, PyUnicode_AsUTF8(process_name));
    Py_DECREF(process_name);
  }

  // Thread Name
  // metadata = self->metadata;
  // while (metadata) {
  //   fprintf(fptr, "{\"ph\":\"M\",\"pid\":%lu,\"tid\":%lu,\"name\":\"thread_name\",\"args\":{\"name\":\"",
  //           pid, metadata->tid);
  //   fprintf(fptr, "\"}},");
  //   metadata = metadata->next;
  // }
  PyObject *key = NULL;
  PyObject *value = NULL;
  while (node) {
    if (!node->filename) {
      node = node->next;
      continue;
    }
    long long ts_long = node->ts / 1000;
    long long dur_long = node->dur / 10;
    PyObject* fname = PyUnicode_Replace(node->filename, PyUnicode_FromString("\\"), PyUnicode_FromString("\\\\"), -1);
    fprintf(
      fptr, "{\"pid\":%lu,\"tid\":%lu,\"ts\":%lld.%03lld,\"ph\":\"X\",\"dur\":%lld.%03lld,\"cat\":\"fee\",\"name\":\"%s (%s)\",\"args\":{\"vars\":[",
      pid,
      node->tid,
      ts_long / 1000,
      ts_long % 1000,
      dur_long / 1000,
      dur_long % 1000,
      PyUnicode_AsUTF8(node->name),
      PyUnicode_AsUTF8(fname)
    );
    Py_DECREF(fname);

    PyObject *args = PyDict_GetItemString(node->args, "func_args");
    if (args) {
      Py_ssize_t pos = 0;
      Py_ssize_t size = PyDict_Size(args);
      while (PyDict_Next(args, &pos, &key, &value)) {
        if (pos != size) {
          fprintf(fptr, "{\"name\": \"%s\", \"type\": \"%s\", \"value\": \"%s\"},",
                  PyUnicode_AsUTF8(key), 
                  PyUnicode_AsUTF8(PyObject_Repr(PyObject_Type(value))),
                  PyUnicode_AsUTF8(PyObject_Repr(value)));
        } else {
          fprintf(fptr, "{\"name\": \"%s\", \"type\": \"%s\", \"value\": \"%s\"}",
                  PyUnicode_AsUTF8(key), 
                  PyUnicode_AsUTF8(PyObject_Repr(PyObject_Type(value))),
                  PyUnicode_AsUTF8(PyObject_Repr(value)));
        }
      }
    }
    fprintf(fptr, "], \"lineno\": %lu}},", node->lineno);
    node = node->next;
  }

  fseek(fptr, -1, SEEK_CUR);
  fprintf(fptr, "]}");
  fclose(fptr);
  OBJTRACKER_THREAD_PROTECT_END(self);
  Py_RETURN_NONE;
}

static PyObject*
objtracker_createthreadinfo(ObjTrackerObject *self)
{
  unsigned long tid = 0;
#if _WIN32
  tid = GetCurrentThreadId();
#elif __APPLE__
  __uint64_t atid = 0;
  if (pthread_threadid_np(NULL, &atid)) {
    tid = (unsigned long)pthread_self();
  } else {
    tid = atid;
  }
#else
  tid = syscall(SYS_gettid);
#endif

  PyGILState_STATE state = PyGILState_Ensure();
  OBJTRACKER_THREAD_PROTECT_START(self);

  PyObject* current_thread_method = PyObject_GetAttrString(thread_module, "current_thread");
  if (!current_thread_method) {
    perror("Failed to access threading.current_thread()");
    exit(-1);
  }
  PyObject* current_thread = PyObject_CallObject(current_thread_method, NULL);
  if (!current_thread) {
    perror("Failed to access threading.current_thread()");
    exit(-1);
  }
  PyObject* thread_name = PyObject_GetAttrString(current_thread, "name");

  Py_DECREF(current_thread_method);
  Py_DECREF(current_thread);

  struct MetadataNode* node = self->metadata;
  int found_node = 0;

  while (node) {
    if (node->tid == tid) {
      Py_DECREF(node->name);
      node->name = thread_name;
      found_node = 1;
      break;
    }
    node = node->next;
  }

  if (!found_node) {
    node = (struct MetadataNode*) PyMem_Calloc(1, sizeof(struct MetadataNode));
    if (!node) {
      perror("Out of memory!");
      exit(-1);
    }
    node->name = thread_name;
    node->tid = tid;
    node->next = self->metadata;
    self->metadata = node;
  }

  if (self->trackernode) {
    self->trackernode->tid = tid;
    self->trackernode->prev_ts = 0.0;
  }

  OBJTRACKER_THREAD_PROTECT_END(self);
  PyGILState_Release(state);
  Py_RETURN_NONE;
}

// ============================================================================
// CodeTracer stuff
// ============================================================================

static PyObject *
ObjTracker_New(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
  ObjTrackerObject *self = (ObjTrackerObject *)type->tp_alloc(type, 0);
  if (self) {
#if _WIN32
    QueryPerformanceFrequency(&qpc_freq);
#endif
    if (!PyArg_ParseTuple(args, "O", &self->pdb)) {
      printf("You need to specify pdb when initializing Tracer\n");
      exit(-1);
    }

    self->breakpoint = 0;
    self->trace_total = 0;
    self->collecting = 0;
    self->log_func_args = 0;
    self->fix_pid = 0;
    self->output_file = NULL;
    self->exclude_files = NULL;
    self->trackernode = NULL;
    self->tracecallback = NULL;
    self->returntrace = NULL;
    self->metadata = NULL;
    objtracker_createthreadinfo(self);
  }

  return (PyObject *)self;
}

static struct PyModuleDef objtrackermodule = {
  PyModuleDef_HEAD_INIT,
  .m_name = "objtracker.tracker",
  .m_doc = "Python interface for the objtracker C library function",
  .m_size = -1
};

static PyTypeObject ObjTrackerType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "objtracker.ObjTracker",
  .tp_doc = "ObjTracker",
  .tp_basicsize = sizeof(ObjTrackerObject),
  .tp_itemsize = 0,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = ObjTracker_New,
  .tp_methods = ObjTrakcer_methods
};

PyMODINIT_FUNC
PyInit_tracker(void)
{
  // ObjTracker Module
  PyObject *m = NULL;

  if (PyType_Ready(&ObjTrackerType) < 0) {
    return NULL;
  }

  m = PyModule_Create(&objtrackermodule);
  if (m == NULL)
    return NULL;
  
  Py_INCREF(&ObjTrackerType);
  if (PyModule_AddObject(m, "ObjTracker", (PyObject *)&ObjTrackerType) < 0)
  {
    Py_DECREF(&ObjTrackerType);
    Py_DECREF(&m);
    return NULL;
  }

  inspect_module = PyImport_ImportModule("inspect");
  traceback_module = PyImport_ImportModule("traceback");
  thread_module = PyImport_ImportModule("threading");
  multiprocessing_module = PyImport_ImportModule("multiprocessing");

  return m;
}
