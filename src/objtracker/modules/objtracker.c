#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>

#include "objtracker.h"
#include "utils.h"

// Function declarations

int objtracker_tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg);
static PyObject* objtracker_ftrace(ObjTrackerObject *self, PyObject *args, PyObject *kwds);

static PyMethodDef ObjTrakcer_methods[] = {
  {"ftrace", (PyCFunction)objtracker_ftrace, METH_VARARGS | METH_KEYWORDS, "trace func callable"},
  {NULL, NULL, 0, NULL}
};

PyObject* inspect_module = NULL;
PyObject* traceback_module = NULL;

// ============================================================================
// Python interface
// ============================================================================

static int
objtracker_tracefuncdisabled(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg)
{
  PyEval_SetTrace(objtracker_tracefunc, obj);
  return objtracker_tracefunc(obj, frame, what, arg);
}

int
objtracker_tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg)
{
  ObjTrackerObject *self = (ObjTrackerObject *)obj;
  PyObject *func_name = frame->f_code->co_name;
  PyObject *filename = frame->f_code->co_filename;
  int lineno = frame->f_lineno;
  struct ObjectNode *node = NULL;

  PyObject *getargvalues_method = PyObject_GetAttrString(inspect_module, "getargvalues");
  if (!getargvalues_method) {
    perror("Failed to access inspect.getargvalues()");
    exit(-1);
  }
  PyObject *args = PyTuple_New(1);
  PyTuple_SetItem(args, 0, (PyObject *) frame);
  Py_INCREF(frame);

  PyFunctionObject *func = NULL;
  PyCodeObject *code = NULL;
  PyObject *varnames = NULL;
  PyObject *argname = NULL;
  PyObject *kwname = NULL;
  PyObject *locals = NULL;

  node = self->trackernode;
  if (what == PyTrace_CALL) {
    while (node) {
      switch (node->type) {
      case PY_FUNCTION:
        func = (PyFunctionObject *)node->obj;
        code = (PyCodeObject *)func->func_code;
        if (PyObject_RichCompareBool(code->co_name, func_name, Py_EQ)) {
          PyObject *argvaluesinfo = PyObject_CallObject(getargvalues_method, args);
          if (!argvaluesinfo) {
            perror("Failed to call inspect.getargvalues()");
            exit(-1);
          }
          Print_Trace_Info(frame, argvaluesinfo, filename, lineno, node->log_stack);
        }
        break;
      case PY_METHOD:
        break;
      default:
        printf("Unknown Node Type!\n");
        exit(1);
      }
      node = node->next;
    }
  }

  Py_DECREF(args);
  Py_DECREF(getargvalues_method);

  return 0;
}

static PyObject *
objtracker_ftrace(ObjTrackerObject *self, PyObject *args, PyObject *kwds)
{
  static char* kwlist[] = {"callable_obj", "frame", "log_stack", NULL};
  PyObject *kw_callable_obj = NULL;
  PyFrameObject *frame = NULL;
  int kw_log_stack = 0;
  int type = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|Oi", kwlist,
        &kw_callable_obj,
        &frame,
        &kw_log_stack)) {
    return NULL;
  }
  if (PyCallable_Check(kw_callable_obj) < 0) {
    PyErr_SetString(PyExc_ValueError, "Is not a callable object");
    return NULL;
  }

  if (PyFunction_Check(kw_callable_obj)) {
    type = PY_FUNCTION;
  } else if (PyMethod_Check(kw_callable_obj)) {
    type = PY_METHOD;
  } else {
    PyErr_SetString(PyExc_ValueError, "Is not a callable object");
    return NULL;
  }

  struct ObjectNode *node = (struct ObjectNode *)PyMem_Calloc(1, sizeof(struct ObjectNode));
  node->obj = kw_callable_obj;
  node->type = type;
  node->log_stack = kw_log_stack;
  if (self->trackernode == NULL) {
    self->trackernode = node;
    self->trackernode->next = NULL;
  } else {
    struct ObjectNode *oldnode = self->trackernode;
    self->trackernode = node;
    self->trackernode->next = oldnode;
  }
  PyEval_SetTrace(objtracker_tracefuncdisabled, (PyObject *)self);

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
