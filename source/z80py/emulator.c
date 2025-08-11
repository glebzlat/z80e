#include <Python.h>

#include <z80/emulator.h>

typedef struct {
  PyObject_HEAD PyObject* memread;
  PyObject* memwrite;
  PyObject* ioread;
  PyObject* iowrite;
  z80e _z80;
  z80e_config config;

  int exc_occurred;
  PyObject* exc_type;
  PyObject* exc_value;
  PyObject* exc_tb;
} Z80;

static int z80_module_exec(PyObject* m);

static int Z80_init(Z80* self, PyObject* args, PyObject* kwargs);
static void Z80_dealloc(Z80* self);

static PyObject* Z80_instruction(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* Z80_dump(PyObject* self, void* closure);
static PyObject* Z80_set_register(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* Z80_get_register(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* Z80_reset(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject* Z80_get_halted(PyObject* self, void* closure);

static zu8 memread_fn(zu32 addr, void* ctx);
static void memwrite_fn(zu32 addr, zu8 byte, void* ctx);
static zu8 ioread_fn(zu16 addr, zu8 byte, void* ctx);
static void iowrite_fn(zu16 addr, zu8 byte, void* ctx);

static PyMethodDef Z80_methods[] = {
    {"instruction", (PyCFunction)Z80_instruction, METH_NOARGS, "Execute one instruction"},
    {"dump", (PyCFunction)Z80_dump, METH_NOARGS, "Get a register dump"},
    {"set_register", (PyCFunction)Z80_set_register, METH_VARARGS, "Set a register value"},
    {"get_register", (PyCFunction)Z80_get_register, METH_VARARGS, "Get a register value"},
    {"reset", (PyCFunction)Z80_reset, METH_NOARGS, "Reset the CPU"},
    {NULL},
};

static PyGetSetDef Z80_getsetters[] = {
    {"halted", (getter)Z80_get_halted, NULL, "Is Z80 halted", NULL},
    {NULL},
};

static PyTypeObject Z80Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0).tp_name = "z80py.Z80",
    .tp_doc = PyDoc_STR("Z80 CPU"),
    .tp_basicsize = sizeof(Z80),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Z80_init,
    .tp_dealloc = (destructor)Z80_dealloc,
    .tp_methods = Z80_methods,
    .tp_getset = Z80_getsetters,
};

static PyModuleDef_Slot z80_module_slots[] = {
    {Py_mod_exec, (void*)z80_module_exec},
    {Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
    {0, NULL},
};

static PyModuleDef z80_module = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "z80py",
    .m_doc = "Z80 emulator Python binding",
    .m_size = 0,
    .m_slots = z80_module_slots,
};

PyMODINIT_FUNC PyInit_z80py(void) { return PyModuleDef_Init(&z80_module); }

#define set_arg(argname)                                                                                               \
  if (!PyCallable_Check(argname)) {                                                                                    \
    PyErr_SetString(PyExc_TypeError, #argname " must be a callable");                                                  \
    return -1;                                                                                                         \
  }                                                                                                                    \
  Py_XSETREF(self->argname, Py_NewRef(argname));

static int Z80_init(Z80* self, PyObject* args, PyObject* kwargs) {
  static char* kwlist[] = {"memread", "memwrite", "ioread", "iowrite", NULL};
  PyObject *memread, *memwrite, *ioread, *iowrite;

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOO", kwlist, &memread, &memwrite, &ioread, &iowrite)) {
    return -1;
  }

  set_arg(memread);
  set_arg(memwrite);
  set_arg(ioread);
  set_arg(iowrite);

  z80e_config cfg = {
      .ctx = self,
      .memread = memread_fn,
      .memwrite = memwrite_fn,
      .ioread = ioread_fn,
      .iowrite = iowrite_fn,
  };
  self->config = cfg;
  z80e_init(&self->_z80, &self->config);

  return 0;
}

static void Z80_dealloc(Z80* self) {
  Py_XDECREF(self->memread);
  Py_XDECREF(self->memwrite);
  Py_XDECREF(self->ioread);
  Py_XDECREF(self->iowrite);
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static inline Z80* self_type(PyObject* obj) {
  if (!PyObject_TypeCheck(obj, &Z80Type)) {
    PyErr_SetString(PyExc_TypeError, "self must be of type Z80");
    return NULL;
  }
  return (Z80*)obj;
}

static PyObject* Z80_instruction(PyObject* self, PyObject* args, PyObject* kwargs) {
  Z80* _self = self_type(self);
  if (!_self)
    return NULL;

  z80e_instruction(&_self->_z80);

  if (_self->exc_occurred) {
    _self->exc_occurred = 0;
    PyErr_Restore(_self->exc_type, _self->exc_value, _self->exc_tb);
    return NULL;
  }

  return Py_None;
}

static PyObject* Z80_get_halted(PyObject* self, void* closure) {
  Z80* _self = self_type(self);
  if (!_self)
    return NULL;
  int result = z80e_get_halt(&_self->_z80);
  return PyBool_FromLong(result);
}

#define set_main_reg(name)                                                                                             \
  tmp = Py_BuildValue("i", _self->_z80.reg.main.name);                                                                 \
  if (!tmp) {                                                                                                          \
    return NULL;                                                                                                       \
  }                                                                                                                    \
  PyDict_SetItemString(dct, #name, tmp);

#define set_alt_reg(name)                                                                                              \
  tmp = Py_BuildValue("i", _self->_z80.reg.alt.name);                                                                  \
  if (!tmp) {                                                                                                          \
    return NULL;                                                                                                       \
  }                                                                                                                    \
  PyDict_SetItemString(dct, #name "_alt", tmp);

#define set_reg(name)                                                                                                  \
  tmp = Py_BuildValue("i", _self->_z80.reg.name);                                                                      \
  if (!tmp) {                                                                                                          \
    return NULL;                                                                                                       \
  }                                                                                                                    \
  PyDict_SetItemString(dct, #name, tmp);

static PyObject* Z80_dump(PyObject* self, void* closure) {
  Z80* _self = self_type(self);
  if (!_self)
    return NULL;

  PyObject* tmp;

  PyObject* dct = PyDict_New();
  if (!dct) {
    return NULL;
  }

  set_main_reg(a);
  set_main_reg(b);
  set_main_reg(c);
  set_main_reg(d);
  set_main_reg(e);
  set_main_reg(h);
  set_main_reg(l);
  set_main_reg(f);
  set_alt_reg(a);
  set_alt_reg(b);
  set_alt_reg(c);
  set_alt_reg(d);
  set_alt_reg(e);
  set_alt_reg(h);
  set_alt_reg(l);
  set_alt_reg(f);
  set_reg(i);
  set_reg(r);
  set_reg(ix);
  set_reg(iy);
  set_reg(sp);
  set_reg(pc);
  set_reg(u);

  return dct;
}

static PyObject* Z80_set_register(PyObject* self, PyObject* args, PyObject* kwargs) {
  Z80* _self = self_type(self);
  if (!_self)
    return NULL;

  char* name;
  int value;
  if (!PyArg_ParseTuple(args, "si", &name, &value))
    return NULL;

  if (strcmp(name, "a") == 0) {
    _self->_z80.reg.main.a = value;
  } else if (strcmp(name, "b") == 0) {
    _self->_z80.reg.main.b = value;
  } else if (strcmp(name, "c") == 0) {
    _self->_z80.reg.main.c = value;
  } else if (strcmp(name, "d") == 0) {
    _self->_z80.reg.main.d = value;
  } else if (strcmp(name, "e") == 0) {
    _self->_z80.reg.main.e = value;
  } else if (strcmp(name, "f") == 0) {
    _self->_z80.reg.main.f = value;
  } else if (strcmp(name, "h") == 0) {
    _self->_z80.reg.main.h = value;
  } else if (strcmp(name, "l") == 0) {
    _self->_z80.reg.main.l = value;
  } else if (strcmp(name, "a_alt") == 0) {
    _self->_z80.reg.alt.a = value;
  } else if (strcmp(name, "b_alt") == 0) {
    _self->_z80.reg.alt.b = value;
  } else if (strcmp(name, "c_alt") == 0) {
    _self->_z80.reg.alt.c = value;
  } else if (strcmp(name, "d_alt") == 0) {
    _self->_z80.reg.alt.d = value;
  } else if (strcmp(name, "e_alt") == 0) {
    _self->_z80.reg.alt.e = value;
  } else if (strcmp(name, "f_alt") == 0) {
    _self->_z80.reg.alt.f = value;
  } else if (strcmp(name, "h_alt") == 0) {
    _self->_z80.reg.alt.h = value;
  } else if (strcmp(name, "l_alt") == 0) {
    _self->_z80.reg.alt.l = value;
  } else if (strcmp(name, "i") == 0) {
    _self->_z80.reg.i = value;
  } else if (strcmp(name, "r") == 0) {
    _self->_z80.reg.r = value;
  } else if (strcmp(name, "pc") == 0) {
    _self->_z80.reg.pc = value;
  } else if (strcmp(name, "sp") == 0) {
    _self->_z80.reg.sp = value;
  } else if (strcmp(name, "ix") == 0) {
    _self->_z80.reg.ix = value;
  } else if (strcmp(name, "iy") == 0) {
    _self->_z80.reg.iy = value;
  } else {
    PyErr_Format(PyExc_ValueError, "no such register: %s", name);
    return NULL;
  }

  return Py_None;
}

static PyObject* Z80_get_register(PyObject* self, PyObject* args, PyObject* kwargs) {
  Z80* _self = self_type(self);
  if (!_self)
    return NULL;

  char* name;
  if (!PyArg_ParseTuple(args, "s", &name))
    return NULL;

  int value;
  if (strcmp(name, "a") == 0) {
    value = _self->_z80.reg.cur->a;
  } else if (strcmp(name, "b") == 0) {
    value = _self->_z80.reg.cur->b;
  } else if (strcmp(name, "c") == 0) {
    value = _self->_z80.reg.cur->c;
  } else if (strcmp(name, "d") == 0) {
    value = _self->_z80.reg.cur->d;
  } else if (strcmp(name, "e") == 0) {
    value = _self->_z80.reg.cur->e;
  } else if (strcmp(name, "f") == 0) {
    value = _self->_z80.reg.cur->f;
  } else if (strcmp(name, "h") == 0) {
    value = _self->_z80.reg.cur->h;
  } else if (strcmp(name, "l") == 0) {
    value = _self->_z80.reg.cur->l;
  } else if (strcmp(name, "i") == 0) {
    value = _self->_z80.reg.i;
  } else if (strcmp(name, "r") == 0) {
    value = _self->_z80.reg.r;
  } else if (strcmp(name, "pc") == 0) {
    value = _self->_z80.reg.pc;
  } else if (strcmp(name, "sp") == 0) {
    value = _self->_z80.reg.sp;
  } else if (strcmp(name, "ix") == 0) {
    value = _self->_z80.reg.ix;
  } else if (strcmp(name, "iy") == 0) {
    value = _self->_z80.reg.iy;
  } else {
    PyErr_SetString(PyExc_ValueError, "no such register");
    return Py_None;
  }

  return PyLong_FromLong(value);
}

static PyObject* Z80_reset(PyObject* self, PyObject* args, PyObject* kwargs) {
  Z80* _self = self_type(self);
  if (!_self)
    return NULL;
  z80e_init(&_self->_z80, &_self->config);
  return Py_None;
}

static zu8 memread_fn(zu32 addr, void* ctx) {
  Z80* self = ctx;
  PyObject *result, *args = Py_BuildValue("(i)", addr);
  int ret = 0;

  if ((result = PyObject_CallObject(self->memread, args)) == NULL) {
    printf("memread_fn: exception\n");
    self->exc_occurred = 1;
    PyErr_Fetch(&self->exc_type, &self->exc_value, &self->exc_tb);
    goto cleanup;
  }

  ret = PyLong_AsInt(result);
  if (ret == -1 && PyErr_Occurred()) {
    printf("memread_fn: exception\n");
    self->exc_occurred = 1;
    PyErr_Fetch(&self->exc_type, &self->exc_value, &self->exc_tb);
    goto cleanup;
  }

cleanup:
  Py_DECREF(args);

  return ret;
}

static void memwrite_fn(zu32 addr, zu8 byte, void* ctx) {
  Z80* self = ctx;
  PyObject* args = Py_BuildValue("(ii)", addr, byte);
  if (PyObject_CallObject(self->memwrite, args) == NULL) {
    self->exc_occurred = 1;
    PyErr_Fetch(&self->exc_type, &self->exc_value, &self->exc_tb);
  }
  Py_DECREF(args);
}

static zu8 ioread_fn(zu16 addr, zu8 byte, void* ctx) {
  Z80* self = ctx;
  PyObject *result, *args = Py_BuildValue("(ii)", addr, byte);
  int ret = 0;

  if ((result = PyObject_CallObject(self->ioread, args)) == NULL) {
    self->exc_occurred = 1;
    PyErr_Fetch(&self->exc_type, &self->exc_value, &self->exc_tb);
    goto cleanup;
  }

  ret = PyLong_AsInt(result);
  if (ret == -1 && PyErr_Occurred()) {
    self->exc_occurred = 1;
    PyErr_Fetch(&self->exc_type, &self->exc_value, &self->exc_tb);
    goto cleanup;
  }

cleanup:
  Py_DECREF(args);

  return ret;
}

static void iowrite_fn(zu16 addr, zu8 byte, void* ctx) {
  Z80* self = ctx;
  PyObject* args = Py_BuildValue("(ii)", addr, byte);
  if (PyObject_CallObject(self->memwrite, args) == NULL) {
    self->exc_occurred = 1;
    PyErr_Fetch(&self->exc_type, &self->exc_value, &self->exc_tb);
  }
  Py_DECREF(args);
}

static int z80_module_exec(PyObject* m) {
  if (PyType_Ready(&Z80Type) < 0) {
    return -1;
  }

  if (PyModule_AddObjectRef(m, "Z80", (PyObject*)&Z80Type) < 0) {
    return -1;
  }

  return 0;
}
