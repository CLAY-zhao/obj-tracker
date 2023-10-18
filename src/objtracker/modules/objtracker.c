#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>

#include "objtracker.h"
#include "utils.h"

#define EQ(left, right) PyObject_RichCompareBool(left, right, Py_EQ)

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
  PyObject *argvaluesinfo = NULL;

  node = self->trackernode;
  if (what == PyTrace_CALL) {
    while (node) {
      switch (node->type) {
      case PY_FUNCTION:
        func = (PyFunctionObject *)node->obj;
        code = (PyCodeObject *)func->func_code;
        if (EQ(code->co_name, func_name)) {
          argvaluesinfo = PyObject_CallObject(getargvalues_method, args);
          if (!argvaluesinfo) {
            perror("Failed to call inspect.getargvalues()");
            exit(-1);
          }
          Py_INCREF(argvaluesinfo);
          Print_Trace_Info(frame, argvaluesinfo, filename, lineno, node->log_stack);
          Py_DECREF(argvaluesinfo);
        }
        break;
      case PY_METHOD:
        func = (PyFunctionObject *)node->obj;
        code = (PyCodeObject *)func->func_code;
        PyObject* __dict__ = PyObject_GetAttrString(node->origin, "__dict__");
        PyObject* function = PyMapping_GetItemString(__dict__, PyUnicode_AsUTF8(code->co_name));
        if (PyFunction_Check(function) && EQ(code->co_name, func_name)) {
          argvaluesinfo = PyObject_CallObject(getargvalues_method, args);
          if (!argvaluesinfo) {
            perror("Failed to call inspect.getargvalues()");
            exit(-1);
          }
          Py_INCREF(argvaluesinfo);
          Print_Trace_Info(frame, argvaluesinfo, filename, lineno, node->log_stack);
          Py_DECREF(argvaluesinfo);
        }
        Py_DECREF(__dict__);
        Py_DECREF(function);
        break;
      case PY_MODULE:
        argvaluesinfo = PyObject_CallObject(getargvalues_method, args);
        if (!argvaluesinfo) {
          perror("Failed to call inspect.getargvalues()");
          exit(-1);
        }
        Py_INCREF(argvaluesinfo);
        Print_Trace_Info(frame, argvaluesinfo, filename, lineno, node->log_stack);
        Py_DECREF(argvaluesinfo);
        break;
      case PY_CLASS:
        PyObject *mro, *base, *dict, *attr;
        mro = PyObject_GetAttrString(node->origin, "__mro__");
        Py_ssize_t n = PyTuple_GET_SIZE(mro);
        for (Py_ssize_t i = 0; i < n; i++) {
          base = PyTuple_GET_ITEM(mro, i);
          assert(PyType_Check(base));
          dict = ((PyTypeObject *)base)->tp_dict;
          assert(dict && PyDict_Check(dict));
          attr = PyDict_GetItem(dict, func_name);
          if (attr != NULL) {
            argvaluesinfo = PyObject_CallObject(getargvalues_method, args);
            if (!argvaluesinfo) {
              perror("Failed to call inspect.getargvalues()");
              exit(-1);
            }
            Py_INCREF(argvaluesinfo);
            Print_Trace_Info(frame, argvaluesinfo, filename, lineno, node->log_stack);
            Py_DECREF(argvaluesinfo);
            break;
          }
        }
        Py_DECREF(mro);
        break;
      default:
        printf("Unknown Node Type!\n");
        exit(1);
      }
      node = node->next;
    }
  } else if (what == PyTrace_LINE) {
    
  }

  Py_DECREF(args);
  Py_DECREF(getargvalues_method);

  return 0;
}

static PyObject *
objtracker_ftrace(ObjTrackerObject *self, PyObject *args, PyObject *kwds)
{
  static char* kwlist[] = {"callable_obj", "frame", "origin", "log_stack", NULL};
  PyObject *kw_callable_obj = NULL;
  PyFrameObject *frame = NULL;
  PyObject *kw_origin = NULL;
  int kw_log_stack = 0;
  int type = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOi", kwlist,
        &kw_callable_obj,
        &frame,
        &kw_origin,
        &kw_log_stack)) {
    return NULL;
  }
  if (PyFunction_Check(kw_callable_obj)) {
    type = PY_FUNCTION;
  } else if (PyMethod_Check(kw_callable_obj)) {
    type = PY_METHOD;
    PyMethodObject *method = (PyMethodObject *)kw_callable_obj;
    kw_callable_obj = (PyObject *)method->im_func;
  } else if (PyModule_Check(kw_callable_obj)) {
    type = PY_MODULE;
  } else {
    type = PY_CLASS;
  }

  struct ObjectNode *node = (struct ObjectNode *)PyMem_Calloc(1, sizeof(struct ObjectNode));
  node->obj = kw_callable_obj;
  node->origin = kw_origin;
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
