#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
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
static PyObject* objtracker_config(ObjTrackerObject *self, PyObject *args, PyObject *kwds);
static PyObject* objtracker_dump(ObjTrackerObject *self, PyObject *args);
static void log_func_args(struct ObjectNode *node, PyFrameObject *frame);

ObjTrackerObject* curr_tracker = NULL;
PyObject* inspect_module = NULL;
PyObject* traceback_module = NULL;
PyObject* multiprocessing_module = NULL;

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
  {"config", (PyCFunction)objtracker_config, METH_VARARGS | METH_KEYWORDS, "config tracker"},
  {"dump", (PyCFunction)objtracker_dump, METH_VARARGS, "dump tracker"},
  {NULL, NULL, 0, NULL}
};

// ============================================================================
// Python interface
// ============================================================================

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
      PyDict_SetItem(func_arg_dict, name, value);
      Py_DECREF(value);
      idx++;
    }

    if (argname != Py_None) {
      PyObject *value = PyDict_GetItem(locals, argname);
      if (!value) {
        value = PyUnicode_FromString("Not Displayable");
        PyErr_Clear();
      }
      PyDict_SetItemString(func_arg_dict, "*args", value);
      Py_DECREF(value);
      idx++;
    }

    if (kwdname != Py_None) {
      PyObject *value = PyDict_GetItem(locals, kwdname);
      if (!value) {
        value = PyUnicode_FromString("Not Displayable");
        PyErr_Clear();
      }
      PyDict_SetItemString(func_arg_dict, "**kwargs", value);
      Py_DECREF(value);
      idx++;
    }

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
    }

    if (self->log_func_args) {
      if (self->trackernode->args) {
        Print_Trace_Info(self->trackernode);
      }
    }
  }

  return 0;
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

  PyObject *key = NULL;
  PyObject *value = NULL;
  while (node) {
    fprintf(
      // fptr, "{\"args\":{\"name\":\"trace\"},\"ph\":\"M\",\"pid\":1,\"tid\":1,\"name\":\"thread_name\",\"filename\":\"%s\",\"call\":\"%s\",\"lineno\":%lu,\"vars\":[",
      fptr, "{\"ph\":\"X\",\"pid\":1,\"tid\":1,\"name\":\"%s\",\"filename\":\"%s\",\"call\":\"%s\",\"lineno\":%lu},",
      PyUnicode_AsUTF8(node->name),
      PyUnicode_AsUTF8(node->filename),
      PyUnicode_AsUTF8(node->name),
      node->lineno
    );

    // PyObject *args = PyDict_GetItemString(node->args, "func_args");

    // if (args) {
    //   Py_ssize_t pos = 0;
    //   Py_ssize_t size = PyDict_Size(args);
    //   while (PyDict_Next(args, &pos, &key, &value)) {
    //     if (pos != size) {
    //       fprintf(fptr, "{\"name\": \"%s\", \"type\": \"%s\", \"value\": \"%s\"},",
    //               PyUnicode_AsUTF8(key), 
    //               PyUnicode_AsUTF8(PyObject_Repr(PyObject_Type(value))),
    //               PyUnicode_AsUTF8(PyObject_Repr(value)));
    //     } else {
    //       fprintf(fptr, "{\"name\": \"%s\", \"type\": \"%s\", \"value\": \"%s\"}",
    //               PyUnicode_AsUTF8(key), 
    //               PyUnicode_AsUTF8(PyObject_Repr(PyObject_Type(value))),
    //               PyUnicode_AsUTF8(PyObject_Repr(value)));
    //     }
    //   }
    // }
    // fprintf(fptr, "]},");
    node = node->next;
  }

  fseek(fptr, -1, SEEK_CUR);
  fprintf(fptr, "]}");
  fclose(fptr);
  OBJTRACKER_THREAD_PROTECT_END(self);
  Py_RETURN_NONE;
}

// ============================================================================
// CodeTracer stuff
// ============================================================================

static PyObject *
ObjTracker_New(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
  ObjTrackerObject *self = (ObjTrackerObject *)type->tp_alloc(type, 0);
  self->trace_total = 0;
  self->collecting = 0;
  self->fix_pid = 0;
  self->output_file = NULL;
  self->trackernode = NULL;
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
  multiprocessing_module = PyImport_ImportModule("multiprocessing");

  return m;
}
