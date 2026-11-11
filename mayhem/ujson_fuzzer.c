/* Port of OSS-Fuzz's ujson_fuzzer.py: decode arbitrary input, re-encode whatever decoded. */
#include "pyfuzz.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc; (void)argv;
  pyfuzz_init();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  PyObject *in = pyfuzz_text_or_bytes(data, size);
  if (!in) pyfuzz_die("could not build input object");
  PyObject *obj = PyObject_CallOneArg(g_ujson_loads, in);
  Py_DECREF(in);
  if (!obj) {
    pyfuzz_check_error();
    PyErr_Clear();
    return -1;
  }
  PyObject *enc = PyObject_CallOneArg(g_ujson_dumps, obj);
  if (!enc) {
    pyfuzz_check_error();
    PyErr_Clear();
  }
  Py_XDECREF(enc);
  Py_DECREF(obj);
  return 0;
}
