/* Port of OSS-Fuzz's json_differential_fuzzer.py: ujson vs the standard library json module. */
#include "pyfuzz.h"

static PyObject *g_json_loads, *g_json_dumps;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc; (void)argv;
  pyfuzz_init();
  PyObject *mod = PyImport_ImportModule("json");
  if (!mod) pyfuzz_die("import json failed");
  g_json_loads = PyObject_GetAttrString(mod, "loads");
  g_json_dumps = PyObject_GetAttrString(mod, "dumps");
  if (!g_json_loads || !g_json_dumps) pyfuzz_die("json.loads/dumps missing");
  Py_DECREF(mod);
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  PyObject *in = PyUnicode_DecodeUTF8((const char *)data, (Py_ssize_t)size, "strict");
  if (!in) {
    PyErr_Clear();
    return -1;
  }
  PyObject *uj = PyObject_CallOneArg(g_ujson_loads, in);
  if (!uj) {
    pyfuzz_check_error();
    PyErr_Clear();
    Py_DECREF(in);
    return -1;
  }
  PyObject *sj = PyObject_CallOneArg(g_json_loads, in);
  if (!sj) {
    PyErr_Clear();
    Py_DECREF(uj);
    Py_DECREF(in);
    return -1;
  }
  PyObject *uj_dumped = PyObject_CallOneArg(g_json_dumps, uj);
  PyObject *sj_dumped = PyObject_CallOneArg(g_json_dumps, sj);
  if (!uj_dumped || !sj_dumped) {
    PyErr_Clear();
  } else if (PyObject_RichCompareBool(uj_dumped, sj_dumped, Py_NE) == 1) {
    fprintf(stderr, "ujson/json decode disagreement\n");
    PyObject_Print(in, stderr, 0);
    fprintf(stderr, "\n  json : ");
    PyObject_Print(sj_dumped, stderr, 0);
    fprintf(stderr, "\n  ujson: ");
    PyObject_Print(uj_dumped, stderr, 0);
    fprintf(stderr, "\n");
    abort();
  }
  Py_XDECREF(uj_dumped);
  Py_XDECREF(sj_dumped);
  Py_DECREF(uj);
  Py_DECREF(sj);
  Py_DECREF(in);
  return 0;
}
