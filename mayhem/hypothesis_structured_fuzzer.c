/* Port of OSS-Fuzz's hypothesis_structured_fuzzer.py: build a JSON-serialisable object plus encoder
 * options from the input bytes and check that decode(encode(obj, **opts)) == obj.
 * The Hypothesis strategies are re-expressed as a byte-cursor generator (like FuzzedDataProvider):
 * atoms are None/bool/int64/finite float/text (no surrogates), containers are list/dict(text keys). */
#include "pyfuzz.h"
#include <math.h>

typedef struct {
  const uint8_t *p;
  size_t n;
} Cursor;

static uint8_t take8(Cursor *c) {
  if (!c->n) return 0;
  c->n--;
  return *c->p++;
}

static uint64_t take64(Cursor *c) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | take8(c);
  return v;
}

static PyObject *gen_text(Cursor *c) {
  size_t len = take8(c) % 24;
  if (len > c->n) len = c->n;
  PyObject *s = PyUnicode_DecodeUTF8((const char *)c->p, (Py_ssize_t)len, "replace");
  c->p += len;
  c->n -= len;
  return s;
}

static PyObject *gen_obj(Cursor *c, int depth) {
  unsigned kind = take8(c) % (depth >= 6 ? 5 : 7);
  switch (kind) {
    case 0: Py_RETURN_NONE;
    case 1: return PyBool_FromLong(take8(c) & 1);
    case 2: return PyLong_FromLongLong((long long)take64(c));
    case 3: {
      uint64_t bits = take64(c);
      double d;
      memcpy(&d, &bits, sizeof d);
      return PyFloat_FromDouble(isfinite(d) ? d : 0.0);
    }
    case 4: return gen_text(c);
    case 5: {
      PyObject *l = PyList_New(0);
      size_t cnt = take8(c) % 6;
      for (size_t i = 0; i < cnt && l; i++) {
        PyObject *item = gen_obj(c, depth + 1);
        if (!item || PyList_Append(l, item) < 0) { Py_XDECREF(item); Py_CLEAR(l); break; }
        Py_DECREF(item);
      }
      return l;
    }
    default: {
      PyObject *d = PyDict_New();
      size_t cnt = take8(c) % 6;
      for (size_t i = 0; i < cnt && d; i++) {
        PyObject *k = gen_text(c);
        PyObject *v = k ? gen_obj(c, depth + 1) : NULL;
        if (!v || PyDict_SetItem(d, k, v) < 0) { Py_XDECREF(k); Py_XDECREF(v); Py_CLEAR(d); break; }
        Py_DECREF(k);
        Py_DECREF(v);
      }
      return d;
    }
  }
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc; (void)argv;
  pyfuzz_init();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  Cursor c = {data, size};
  uint8_t flags = take8(&c);
  uint8_t indent = take8(&c) % 21;
  PyObject *obj = gen_obj(&c, 0);
  if (!obj) pyfuzz_die("object generation failed");

  PyObject *kwargs = Py_BuildValue("{s:O,s:O,s:O,s:O,s:i}",
      "ensure_ascii", (flags & 1) ? Py_True : Py_False,
      "encode_html_chars", (flags & 2) ? Py_True : Py_False,
      "escape_forward_slashes", (flags & 4) ? Py_True : Py_False,
      "sort_keys", (flags & 8) ? Py_True : Py_False,
      "indent", (int)indent);
  PyObject *args = PyTuple_Pack(1, obj);
  if (!kwargs || !args) pyfuzz_die("kwargs construction failed");

  PyObject *enc = PyObject_Call(g_ujson_dumps, args, kwargs);
  Py_DECREF(args);
  Py_DECREF(kwargs);
  if (!enc) {
    pyfuzz_check_error();
    fprintf(stderr, "ujson.dumps rejected a plain JSON-serialisable object\n");
    PyObject_Print(obj, stderr, 0);
    fprintf(stderr, "\n");
    pyfuzz_die("encode failed");
  }
  PyObject *back = PyObject_CallOneArg(g_ujson_loads, enc);
  if (!back) {
    fprintf(stderr, "ujson.loads rejected ujson.dumps output\n");
    PyObject_Print(enc, stderr, 0);
    fprintf(stderr, "\n");
    pyfuzz_die("decode of own output failed");
  }
  if (PyObject_RichCompareBool(obj, back, Py_EQ) != 1) {
    fprintf(stderr, "round-trip mismatch\n  obj : ");
    PyObject_Print(obj, stderr, 0);
    fprintf(stderr, "\n  json: ");
    PyObject_Print(enc, stderr, 0);
    fprintf(stderr, "\n  back: ");
    PyObject_Print(back, stderr, 0);
    fprintf(stderr, "\n");
    abort();
  }
  Py_DECREF(back);
  Py_DECREF(enc);
  Py_DECREF(obj);
  return 0;
}
