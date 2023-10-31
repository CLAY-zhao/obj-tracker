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

// Function declarations

int objtracker_tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg);
static PyObject* objtracker_start(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_stop(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_addtracehook(PyObject *obj, PyObject *args, PyObject *kwds);
static PyObject* objtracker_config(ObjTrackerObject *self, PyObject *args, PyObject *kwds);
static PyObject* objtracker_dump(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_createthreadinfo(ObjTrackerObject *self);
static void trigger_trace_hook(ObjTrackerObject *self);
static void log_func_args(struct ObjectNode *node, PyFrameObject *frame);

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
  {"add_trace_hook", (PyCFunction)objtracker_addtracehook, METH_VARARGS | METH_KEYWORDS, "add trace hook"},
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
  while (tracehook) {
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
    tracehook = tracehook->next;
  }

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
        PyDict_SetItem(func_arg_dict, name, PyUnicode_FromString("too long..."));
      } else if (PyBytes_Check(value)) {
        PyDict_SetItem(func_arg_dict, name, PyUnicode_FromString("too long.."));
      } else if (PyCode_Check(value)) {
        PyDict_SetItem(func_arg_dict, name, PyUnicode_FromString("too long..."));
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
    printf("Warning! Overwrite tracker! You should not have two ObjTracker recording at the same time");
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

int
objtracker_tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg)
{
  ObjTrackerObject *self = (ObjTrackerObject *)obj;
  frame->f_trace_opcodes = 1;

  if (!self->collecting) {
    PyEval_SetTrace(objtracker_tracefuncdisabled, obj);
    return 0;
  }

  if (what == PyTrace_CALL) {
    int is_call = (what == PyTrace_CALL || what == PyTrace_C_CALL);
    int is_return = (what == PyTrace_RETURN || what == PyTrace_C_RETURN || what == PyTrace_C_EXCEPTION);

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
      
      if (self->tracecallback) {
        trigger_trace_hook(self);
      }
    }

  } else if (what == PyTrace_RETURN) {
    if (self->trackernode) {
      double dur = get_ts(self->trackernode) - self->trackernode->ts;
      self->trackernode->dur = dur;
    }
  }

  return 0;
}

static PyObject*
objtracker_addtracehook(PyObject *obj, PyObject *args, PyObject *kwds)
{
  static char* kwlist[] = {"callback", "alias", "when_type_trigger", "when_value_trigger", NULL};
  PyObject *kw_callback = NULL;
  char* kw_alias = NULL;
  PyObject *kw_when_type_trigger = NULL;
  PyObject *kw_when_value_trigger = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OsOO", kwlist,
        &kw_callback,
        &kw_alias,
        &kw_when_type_trigger,
        &kw_when_value_trigger)) {
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

  if (PyIter_Check(kw_when_type_trigger)) {
    self->tracecallback->when_type_trigger = kw_when_type_trigger;
  }

  if (PyIter_Check(kw_when_value_trigger)) {
    self->tracecallback->when_value_trigger = kw_when_value_trigger;
  }
  
  Py_RETURN_NONE;
}

static PyObject*
objtracker_config(ObjTrackerObject *self, PyObject *args, PyObject *kwds)
{
  static char* kwlist[] = {"log_func_args", "output_file", 
          NULL};
  int kw_log_func_args = 0;
  char* kw_output_file = NULL;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|is", kwlist,
        &kw_log_func_args,
        &kw_output_file)) {
      return NULL;
  }

  if (kw_log_func_args >= 0) {
    self->log_func_args = kw_log_func_args;
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
    fprintf(fptr, "{\"ph\":\"M\",\"pid\":%lu,\"tid\":%lu,\"name\":\"process_name\",\"args\":{\"name\":\"%s\"}},",
            pid, pid, PyUnicode_AsUTF8(process_name));
    Py_DECREF(process_name);
  }

  // Thread Name
  metadata = self->metadata;
  while (metadata) {
    fprintf(fptr, "{\"ph\":\"M\",\"pid\":%lu,\"tid\":%lu,\"name\":\"thread_name\",\"args\":{\"name\":\"",
            pid, metadata->tid);
    fprintf(fptr, "\"}},");
    metadata = metadata->next;
  }

  PyObject *key = NULL;
  PyObject *value = NULL;
  while (node) {
    if (!node->filename) {
      node = node->next;
      continue;
    }
    long long ts_long = node->ts;
    long long dur_long = node->dur;
    fprintf(
      fptr, "{\"pid\":%lu,\"tid\":%lu,\"ts\":%lld.%03lld,\"ph\":\"X\",\"dur\":%lld.%03lld,\"cat\":\"fee\",\"name\":\"%s (%s)\",\"args\":{\"vars\":[",
      pid,
      node->tid,
      ts_long / 1000,
      ts_long % 1000,
      dur_long / 1000,
      dur_long % 1000,
      PyUnicode_AsUTF8(node->name),
      PyUnicode_AsUTF8(node->filename)
    );

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
    QueryPerformanceCounter(&qpc_freq);
    self->trace_total = 0;
    self->collecting = 0;
    self->fix_pid = 0;
    self->output_file = NULL;
    self->trackernode = NULL;
    self->tracecallback = NULL;
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
