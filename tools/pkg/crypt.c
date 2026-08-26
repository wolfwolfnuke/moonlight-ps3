#define PY_SSIZE_T_CLEAN 1

#include <Python.h>

static PyObject *sha1_callback = NULL;

#if PY_MAJOR_VERSION >= 3
#define BYTES "y"
#else
#define BYTES "s"
#endif

static void manipulate(uint8_t *key) {
	/* We need to cast each byte to a 64 bit int so that gcc won't truncate it
	   down to a 32 bit in before shifting */
	uint64_t temp = ((uint64_t) key[0x38]) << 56|
			((uint64_t) key[0x39]) << 48|
			((uint64_t) key[0x3a]) << 40|
			((uint64_t) key[0x3b]) << 32|
			key[0x3c] << 24|
			key[0x3d] << 16|
			key[0x3e] <<  8|
			key[0x3f];
	temp++;
	key[0x38] = (temp >> 56) & 0xff;
	key[0x39] = (temp >> 48) & 0xff;
	key[0x3a] = (temp >> 40) & 0xff;
	key[0x3b] = (temp >> 32) & 0xff;
	key[0x3c] = (temp >> 24) & 0xff;
	key[0x3d] = (temp >> 16) & 0xff;
	key[0x3e] = (temp >>  8) & 0xff;
	key[0x3f] = (temp >>  0) & 0xff;
}

static PyObject* pkg_crypt(PyObject *self, PyObject *args) {
	uint8_t *key, *input, *ret;
	Py_ssize_t key_length, input_length;
	int length, remaining, i, offset=0;

	PyObject *arglist;
	PyObject *result;

	if (!PyArg_ParseTuple(args, BYTES "#" BYTES "#i", &key, &key_length, &input, &input_length, &length))
		return NULL;
	ret = malloc(length);
	remaining = length;
	
	while (remaining > 0) {
		int bytes_to_dump = remaining;
		if (bytes_to_dump > 0x10) 
			bytes_to_dump = 0x10;

		// outhash = SHA1(listToString(key)[0:0x40])
		uint8_t *outHash; 
		{
			arglist = Py_BuildValue("(" BYTES "#)", key, (Py_ssize_t)0x40);
			result = PyObject_CallObject(sha1_callback, arglist);
			Py_DECREF(arglist);
			if (!result) return NULL;
			Py_ssize_t outHash_length;
			if (!PyArg_Parse(result, BYTES "#", &outHash, &outHash_length)) return NULL;
		}
		for(i = 0; i < bytes_to_dump; i++) {
			ret[offset] = outHash[i] ^ input[offset];
			offset++;
		}
		Py_DECREF(result);
		manipulate(key);
		remaining -= bytes_to_dump;
	}
	
	// Return the encrypted data
	PyObject *py_ret = Py_BuildValue(BYTES "#", ret, length);
	free(ret);
	return py_ret;
}

static PyObject *register_sha1_callback(PyObject *self, PyObject *args) {
	PyObject *result = NULL;
	PyObject *temp;

	if (PyArg_ParseTuple(args, "O:set_callback", &temp)) {
        	if (!PyCallable_Check(temp)) {
		    	PyErr_SetString(PyExc_TypeError, "parameter must be callable");
	    		return NULL;
		}
		Py_XINCREF(temp);         /* Add a reference to new callback */
		Py_XDECREF(sha1_callback);  /* Dispose of previous callback */
		sha1_callback = temp;       /* Remember new callback */
		/* Boilerplate to return "None" */
		Py_INCREF(Py_None);
		result = Py_None;
	}
	return result;
}

static PyMethodDef cryptMethods[] = {
	{"pkgcrypt", pkg_crypt, METH_VARARGS, "C implementation of pkg.py's crypt function"},
	{"register_sha1_callback", register_sha1_callback, METH_VARARGS, "Register a callback to python's SHA1 function, so we don't have to bother with creating our own implementation."},
	{NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3

static struct PyModuleDef pkgcryptModDef =
{
	PyModuleDef_HEAD_INIT,
	"pkgcrypt",
	NULL,
	-1,
	cryptMethods
};

PyMODINIT_FUNC PyInit_pkgcrypt(void) {
	return PyModule_Create(&pkgcryptModDef);
}

#else

PyMODINIT_FUNC initpkgcrypt(void) {
	(void) Py_InitModule("pkgcrypt", cryptMethods);
}

#endif
