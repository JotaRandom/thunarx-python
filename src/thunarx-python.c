/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 *  Copyright (C) 2004,2005 Johan Dahlin
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <Python.h>
#include <pygobject.h>
#include <pygtk/pygtk.h>

#include "thunarx-python.h"
#include "thunarx-python-object.h"
#include "pygnomevfs.h"

#include <thunarx/thunarx-extension-types.h>

static const GDebugKey thunarx_python_debug_keys[] = {
  {"misc", NAUTILUS_PYTHON_DEBUG_MISC},
};
static const guint thunarx_python_ndebug_keys = sizeof (thunarx_python_debug_keys) / sizeof (GDebugKey);
ThunarxPythonDebug thunarx_python_debug;

static gboolean thunarx_python_init_python(void);

static GArray *all_types = NULL;


static inline gboolean np_init_pygobject(void)
{
    PyObject *gobject = PyImport_ImportModule("gobject");
    if (gobject != NULL) {
        PyObject *mdict = PyModule_GetDict(gobject);
        PyObject *cobject = PyDict_GetItemString(mdict, "_PyGObject_API");
        if (PyCObject_Check(cobject))
            _PyGObject_API = (struct _PyGObject_Functions *)PyCObject_AsVoidPtr(cobject);
        else {
            PyErr_SetString(PyExc_RuntimeError,
                            "could not find _PyGObject_API object");
			PyErr_Print();
			return FALSE;
        }
    } else {
        PyErr_Print();
        g_warning("could not import gobject");
        return FALSE;
    }
	return TRUE;
}

static inline gboolean np_init_pygtk(void)
{
    PyObject *pygtk = PyImport_ImportModule("gtk._gtk");
    if (pygtk != NULL) {
		PyObject *module_dict = PyModule_GetDict(pygtk);
		PyObject *cobject = PyDict_GetItemString(module_dict, "_PyGtk_API");
		if (PyCObject_Check(cobject))
			_PyGtk_API = (struct _PyGtk_FunctionStruct*)
				PyCObject_AsVoidPtr(cobject);
		else {
            PyErr_SetString(PyExc_RuntimeError,
                            "could not find _PyGtk_API object");
			PyErr_Print();
			return FALSE;
        }
    } else {
        PyErr_Print();
        g_warning("could not import gtk._gtk");
        return FALSE;
    }
	return TRUE;
}

static inline gboolean np_init_pygnomevfs(void)
{
    PyObject *module = PyImport_ImportModule("gnomevfs");
    if (module != NULL) {
        PyObject *mdict = PyModule_GetDict(module);
        PyObject *cobject = PyDict_GetItemString(mdict, "_PyGnomeVFS_API");
        if (PyCObject_Check(cobject))
            _PyGnomeVFS_API = (struct _PyGnomeVFS_Functions *)PyCObject_AsVoidPtr(cobject);
        else {
	         g_warning("could not find _PyGnomeVFS_API object in the gnomevfs module");
			 return FALSE;
        }
    } else {
        PyErr_Print();
        g_warning("could not import gnomevfs");
		return FALSE;
    }
	return TRUE;
}


static void
thunarx_python_load_file(GTypeModule *type_module, const gchar *filename)
{
	PyObject *main_module, *main_locals, *locals, *key, *value;
	PyObject *module;
	GType gtype;
	Py_ssize_t pos = 0;
	
	debug_enter_args("filename=%s", filename);
	
	main_module = PyImport_AddModule("__main__");
	if (main_module == NULL) {
		g_warning("Could not get __main__.");
		return;
	}
	
	main_locals = PyModule_GetDict(main_module);
	module = PyImport_ImportModuleEx((char *) filename, main_locals, main_locals, NULL);
	if (!module) {
		PyErr_Print();
		return;
	}
	
	locals = PyModule_GetDict(module);
	
	while (PyDict_Next(locals, &pos, &key, &value)) {
		if (!PyType_Check(value))
			continue;

		if (PyObject_IsSubclass(value, (PyObject*)&PyThunarxColumnProvider_Type) ||
			PyObject_IsSubclass(value, (PyObject*)&PyThunarxInfoProvider_Type) ||
			PyObject_IsSubclass(value, (PyObject*)&PyThunarxLocationWidgetProvider_Type) ||
			PyObject_IsSubclass(value, (PyObject*)&PyThunarxMenuProvider_Type) ||
			PyObject_IsSubclass(value, (PyObject*)&PyThunarxPropertyPageProvider_Type)) {
			
			gtype = thunarx_python_object_get_type(type_module, value);
			g_array_append_val(all_types, gtype);
		}
	}
	
	debug("Loaded python modules");
}

static void
thunarx_python_load_dir (GTypeModule *module, const char *dirname)
{
	GDir *dir;
	const char *name;
	gboolean initialized = FALSE;

	debug_enter_args("dirname=%s", dirname);
	
	dir = g_dir_open(dirname, 0, NULL);
	if (!dir)
		return;

			
	while ((name = g_dir_read_name(dir))) {
		if (g_str_has_suffix(name, ".py")) {
			char *modulename;
			int len;

			len = strlen(name) - 3;
			modulename = g_new0(char, len + 1 );
			strncpy(modulename, name, len);

			if (!initialized) {
				PyObject *sys_path, *py_path;
				  /* n-p python part is initialized on demand (or not
				   * at all if no extensions are found) */
				if (!thunarx_python_init_python()) {
					g_warning("thunarx_python_init_python failed");
					goto exit;
				}
				  /* sys.path.insert(0, dirname) */
				sys_path = PySys_GetObject("path");
				py_path = PyString_FromString(dirname);
				PyList_Insert(sys_path, 0, py_path);
				Py_DECREF(py_path);
			}
			thunarx_python_load_file(module, modulename);
		}
	}
exit:
	g_dir_close(dir);
}

static gboolean
thunarx_python_init_python (void)
{
	PyObject *pygtk, *mdict, *require;
	PyObject *sys_path, *tmp, *thunarx, *gtk, *pygtk_version, *pygtk_required_version;
	GModule *libpython;
	char *argv[] = { "thunarx", NULL };

	if (Py_IsInitialized())
		return TRUE;

  	debug("g_module_open " PY_LIB_LOC "/libpython" PYTHON_VERSION "." G_MODULE_SUFFIX);
	libpython = g_module_open(PY_LIB_LOC "/libpython" PYTHON_VERSION "." G_MODULE_SUFFIX, 0);
	if (!libpython)
		g_warning("g_module_open libpython failed: %s", g_module_error());
	debug("Py_Initialize");
	Py_Initialize();
	if (PyErr_Occurred()) {
		PyErr_Print();
		return FALSE;
	}
	
	debug("PySys_SetArgv");
	PySys_SetArgv(1, argv);
	if (PyErr_Occurred()) {
		PyErr_Print();
		return FALSE;
	}

	/* pygtk.require("2.0") */
	debug("pygtk.require(\"2.0\")");
	pygtk = PyImport_ImportModule("pygtk");
	if (!pygtk) {
		PyErr_Print();
		return FALSE;
	}
	mdict = PyModule_GetDict(pygtk);
	require = PyDict_GetItemString(mdict, "require");
	PyObject_CallObject(require, Py_BuildValue("(S)", PyString_FromString("2.0")));
	if (PyErr_Occurred()) {
		PyErr_Print();
		return FALSE;
	}

	/* import gobject */
  	debug("init_pygobject");
	if (!np_init_pygobject()) {
		g_warning("pygobject initialization failed");
		return FALSE;
	}
	/* import gtk */
	debug("init_pygtk");
	if (!np_init_pygtk()) {
		g_warning("pygtk initialization failed");
		return FALSE;
	}
	/* import gnomevfs */
	debug("init_gnomevfs");
	if (!np_init_pygnomevfs())
		return FALSE;

	/* gobject.threads_init() */
    debug("pyg_enable_threads");
	setenv("PYGTK_USE_GIL_STATE_API", "", 0);
	pyg_enable_threads();

	/* gtk.pygtk_version < (2, 4, 0) */
	gtk = PyImport_ImportModule("gtk");
	mdict = PyModule_GetDict(gtk);
	pygtk_version = PyDict_GetItemString(mdict, "pygtk_version");
	pygtk_required_version = Py_BuildValue("(iii)", 2, 4, 0);
	if (PyObject_Compare(pygtk_version, pygtk_required_version) == -1) {
		g_warning("PyGTK %s required, but %s found.",
				  PyString_AsString(PyObject_Repr(pygtk_required_version)),
				  PyString_AsString(PyObject_Repr(pygtk_version)));
		Py_DECREF(pygtk_required_version);
		return FALSE;
	}
	Py_DECREF(pygtk_required_version);
	
	/* sys.path.insert(., ...) */
	debug("sys.path.insert(0, ...)");
	sys_path = PySys_GetObject("path");
	PyList_Insert(sys_path, 0,
				  (tmp = PyString_FromString(NAUTILUS_LIBDIR "/thunarx-python")));
	Py_DECREF(tmp);
	
	/* import thunarx */
	g_setenv("INSIDE_NAUTILUS_PYTHON", "", FALSE);
	debug("import thunarx");
	thunarx = PyImport_ImportModule("thunarx");
	if (!thunarx) {
		PyErr_Print();
		return FALSE;
	}

	/* Extract types and interfaces from thunarx */
	mdict = PyModule_GetDict(thunarx);
	
	_PyGtkWidget_Type = pygobject_lookup_class(GTK_TYPE_WIDGET);
	g_assert(_PyGtkWidget_Type != NULL);

#define IMPORT(x, y) \
    _PyThunarx##x##_Type = (PyTypeObject *)PyDict_GetItemString(mdict, y); \
	if (_PyThunarx##x##_Type == NULL) { \
		PyErr_Print(); \
		return FALSE; \
	}

	IMPORT(Column, "Column");
	IMPORT(ColumnProvider, "ColumnProvider");
	IMPORT(InfoProvider, "InfoProvider");
	IMPORT(LocationWidgetProvider, "LocationWidgetProvider");
	IMPORT(Menu, "Menu");
	IMPORT(MenuItem, "MenuItem");
	IMPORT(MenuProvider, "MenuProvider");
	IMPORT(PropertyPage, "PropertyPage");
	IMPORT(PropertyPageProvider, "PropertyPageProvider");

#undef IMPORT
	
	return TRUE;
}

void
thunarx_module_initialize(GTypeModule *module)
{
	gchar *user_extensions_dir;
	const gchar *env_string;

	env_string = g_getenv("NAUTILUS_PYTHON_DEBUG");
	if (env_string != NULL) {
		thunarx_python_debug = g_parse_debug_string(env_string,
													 thunarx_python_debug_keys,
													 thunarx_python_ndebug_keys);
		env_string = NULL;
    }
	
	debug_enter();

	all_types = g_array_new(FALSE, FALSE, sizeof(GType));

	thunarx_python_load_dir(module, NAUTILUS_EXTENSION_DIR "/python");
	user_extensions_dir = g_strdup_printf("%s/.thunarx/python-extensions/",
										  g_get_home_dir());
	thunarx_python_load_dir(module, user_extensions_dir);
	g_free(user_extensions_dir);
}
 
void
thunarx_module_shutdown(void)
{
	debug_enter();
	if (Py_IsInitialized()) {
		Py_Finalize();
	}
	g_array_free(all_types, TRUE);
}

void 
thunarx_module_list_types(const GType **types,
						   int          *num_types)
{
	debug_enter();
	
	*types = (GType*)all_types->data;
	*num_types = all_types->len;
}
