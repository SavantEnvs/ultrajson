/* Shared CPython-embedding helpers for the ujson libFuzzer harnesses.
 * The ujson C sources are compiled (sanitized + SanCov) straight into the harness binary and
 * registered as a built-in module, so the fuzzed code is instrumented while the interpreter
 * (system libpython) is not. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern PyObject *PyInit_ujson(void);

static PyObject *g_ujson_loads, *g_ujson_dumps;

static void pyfuzz_die(const char *what) {
  fprintf(stderr, "ujson harness: %s\n", what);
  if (PyErr_Occurred()) PyErr_Print();
  abort();
}

static void pyfuzz_init(void) {
  /* PYTHONMALLOC=malloc routes the interpreter's allocations (and ujson's PyObject_Malloc)
     through libc malloc, so ASan sees ujson's heap buffers. */
  setenv("PYTHONMALLOC", "malloc", 1);
  PyImport_AppendInittab("ujson", PyInit_ujson);
  PyConfig config;
  PyConfig_InitPythonConfig(&config);
  config.install_signal_handlers = 0;
  config.parse_argv = 0;
  config.site_import = 0;
  config.user_site_directory = 0;
  config.write_bytecode = 0;
  config.use_hash_seed = 1;
  config.hash_seed = 0;
  PyStatus st = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  if (PyStatus_Exception(st)) pyfuzz_die("Py_InitializeFromConfig failed");
  PyObject *mod = PyImport_ImportModule("ujson");
  if (!mod) pyfuzz_die("import ujson failed");
  g_ujson_loads = PyObject_GetAttrString(mod, "loads");
  g_ujson_dumps = PyObject_GetAttrString(mod, "dumps");
  if (!g_ujson_loads || !g_ujson_dumps) pyfuzz_die("ujson.loads/dumps missing");
  Py_DECREF(mod);
}

/* A SystemError means the C extension broke the CPython calling contract (NULL without an
   exception, or a result with one set) -- that is a ujson bug. Every other exception is the
   library's documented way to reject an input. */
static void pyfuzz_check_error(void) {
  if (PyErr_ExceptionMatches(PyExc_SystemError)) pyfuzz_die("SystemError from ujson");
}

/* The input as the fuzzer-chosen text: UTF-8 str when valid, otherwise the raw bytes object
   (ujson.loads accepts both, and only the bytes path reaches the decoder's UTF-8 validation). */
static PyObject *pyfuzz_text_or_bytes(const uint8_t *data, size_t size) {
  PyObject *s = PyUnicode_DecodeUTF8((const char *)data, (Py_ssize_t)size, "strict");
  if (s) return s;
  PyErr_Clear();
  return PyBytes_FromStringAndSize((const char *)data, (Py_ssize_t)size);
}
