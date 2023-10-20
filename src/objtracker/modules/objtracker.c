#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>

#include "pythoncapi_compat.h"
#include "objtracker.h"
#include "utils.h"

#define EQ(left, right) PyObject_RichCompareBool(left, right, Py_EQ)

// Function declarations

int objtracker_tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg);
static PyObject* objtracker_start(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_stop(ObjTrackerObject *self, PyObject *args);
static PyObject* objtracker_config(ObjTrackerObject *self, PyObject *args, PyObject *kwds);
static void log_func_args(struct ObjectNode *node, PyFrameObject *frame);

ObjTrackerObject* curr_tracker = NULL;
PyObject* inspect_module = NULL;
PyObject* traceback_module = NULL;

static PyMethodDef ObjTrakcer_methods[] = {
  {"start", (PyCFunction)objtracker_start, METH_VARARGS, "start tracker"},
  {"stop", (PyCFunction)objtracker_stop, METH_VARARGS, "stop tracker"},
  {"config", (PyCFunction)objtracker_config, METH_VARARGS | METH_KEYWORDS, "config tracker"},
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

  if (!PyObject_Repr(arginfo)) {
    return 0;
  }

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

exit:
  Py_XDECREF(code);
  Py_XDECREF(names);
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
    PyMem_FREE(self->trackernode);
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

static PyObject *
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

// ============================================================================
// CodeTracer stuff
// ============================================================================

static PyObject *
ObjTracker_New(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
  ObjTrackerObject *self = (ObjTrackerObject *)type->tp_alloc(type, 0);
  self->trace_total = 0;
  self->collecting = 0;
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

  return m;
}
