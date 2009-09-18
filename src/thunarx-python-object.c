/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 *  Copyright (C) 2004 Novell, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Dave Camp <dave@ximian.com>
 * 
 */

#include <config.h>

#include "thunarx-python-object.h"
#include "thunarx-python.h"

#include <thunarx/thunarx-extension-types.h>

#include <pygobject.h>

/* Thunarx extension headers */
#include <thunarx/thunarx-file-info.h>
#include <thunarx/thunarx-info-provider.h>
#include <thunarx/thunarx-column-provider.h>
#include <thunarx/thunarx-location-widget-provider.h>
#include <thunarx/thunarx-menu-item.h>
#include <thunarx/thunarx-menu-provider.h>
#include <thunarx/thunarx-property-page-provider.h>

#include <string.h>

#define METHOD_PREFIX ""

static GObjectClass *parent_class;

/* These macros assumes the following things:
 *   a METHOD_NAME is defined with is a string
 *   a goto label called beach
 *   the return value is called ret
 */

#define CHECK_METHOD_NAME(self)                                        \
	if (!PyObject_HasAttrString(self, METHOD_NAME))                    \
		goto beach;
	
#define CONVERT_LIST(py_files, files)                                  \
	{                                                                  \
		GList *l;                                                      \
        py_files = PyList_New(0);                                      \
		for (l = files; l; l = l->next) {                              \
            PyObject *obj = pygobject_new((GObject*)l->data);          \
			PyList_Append(py_files, obj);							   \
			Py_DECREF(obj);                                            \
		}                                                              \
	}

#define HANDLE_RETVAL(py_ret)                                          \
    if (!py_ret) {                                                     \
		PyErr_Print();                                                 \
		goto beach;                                                    \
 	} else if (py_ret == Py_None) {                                    \
 		goto beach;                                                    \
	}

#define HANDLE_LIST(py_ret, type, type_name)                           \
    {                                                                  \
        Py_ssize_t i = 0;                                                     \
    	if (!PySequence_Check(py_ret) || PyString_Check(py_ret)) {     \
    		PyErr_SetString(PyExc_TypeError,                           \
    						METHOD_NAME " must return a sequence");    \
    		goto beach;                                                \
    	}                                                              \
    	for (i = 0; i < PySequence_Size (py_ret); i++) {               \
    		PyGObject *py_item;                                        \
    		py_item = (PyGObject*)PySequence_GetItem (py_ret, i);      \
    		if (!pygobject_check(py_item, &Py##type##_Type)) {         \
    			PyErr_SetString(PyExc_TypeError,                       \
    							METHOD_NAME                            \
    							" must return a sequence of "          \
    							type_name);                            \
    			goto beach;                                            \
    		}                                                          \
    		ret = g_list_append (ret, (type*) g_object_ref(py_item->obj)); \
            Py_DECREF(py_item);                                        \
    	}                                                              \
    }

#define METHOD_NAME "get_property_pages"
static GList *
thunarx_python_object_get_property_pages (ThunarxPropertyPageProvider *provider,
										   GList *files)
{
	ThunarxPythonObject *object = (ThunarxPythonObject*)provider;
    PyObject *py_files, *py_ret = NULL;
    GList *ret = NULL;
	PyGILState_STATE state = pyg_gil_state_ensure();                                    \
	
  	debug_enter();

	CHECK_METHOD_NAME(object->instance);

	CONVERT_LIST(py_files, files);
	
    py_ret = PyObject_CallMethod(object->instance, METHOD_PREFIX METHOD_NAME,
								 "(N)", py_files);
	HANDLE_RETVAL(py_ret);

	HANDLE_LIST(py_ret, ThunarxPropertyPage, "thunarx.PropertyPage");
	
 beach:
	Py_XDECREF(py_ret);
	pyg_gil_state_release(state);
    return ret;
}
#undef METHOD_NAME


static void
thunarx_python_object_property_page_provider_iface_init (ThunarxPropertyPageProviderIface *iface)
{
	iface->get_pages = thunarx_python_object_get_property_pages;
}

#define METHOD_NAME "get_widget"
static GtkWidget *
thunarx_python_object_get_widget (ThunarxLocationWidgetProvider *provider,
								   const char *uri,
								   GtkWidget *window)
{
	ThunarxPythonObject *object = (ThunarxPythonObject*)provider;
	GtkWidget *ret = NULL;
	PyObject *py_ret = NULL;
	PyGObject *py_ret_gobj;
	PyObject *py_uri = NULL;
	PyGILState_STATE state = pyg_gil_state_ensure();

	debug_enter();

	CHECK_METHOD_NAME(object->instance);

	py_uri = PyString_FromString(uri);

	py_ret = PyObject_CallMethod(object->instance, METHOD_PREFIX METHOD_NAME,
								 "(NN)", py_uri,
								 pygobject_new((GObject *)window));
	HANDLE_RETVAL(py_ret);

	py_ret_gobj = (PyGObject *)py_ret;
	if (!pygobject_check(py_ret_gobj, &PyGtkWidget_Type)) {
		PyErr_SetString(PyExc_TypeError,
					    METHOD_NAME "should return a gtk.Widget");
		goto beach;
	}
	ret = (GtkWidget *)g_object_ref(py_ret_gobj->obj);

 beach:
	Py_XDECREF(py_ret);
	pyg_gil_state_release(state);
	return ret;
}
#undef METHOD_NAME

static void
thunarx_python_object_location_widget_provider_iface_init (ThunarxLocationWidgetProviderIface *iface)
{
	iface->get_widget = thunarx_python_object_get_widget;
}

#define METHOD_NAME "get_file_items"
static GList *
thunarx_python_object_get_file_items (ThunarxMenuProvider *provider,
									   GtkWidget *window,
									   GList *files)
{
	ThunarxPythonObject *object = (ThunarxPythonObject*)provider;
    GList *ret = NULL;
    PyObject *py_ret = NULL, *py_files;
	PyGILState_STATE state = pyg_gil_state_ensure();
	
  	debug_enter();

	CHECK_METHOD_NAME(object->instance);

	CONVERT_LIST(py_files, files);

    py_ret = PyObject_CallMethod(object->instance, METHOD_PREFIX METHOD_NAME,
								 "(NN)", pygobject_new((GObject *)window), py_files);
	HANDLE_RETVAL(py_ret);

	HANDLE_LIST(py_ret, ThunarxMenuItem, "thunarx.MenuItem");

 beach:
	Py_XDECREF(py_ret);
	pyg_gil_state_release(state);
    return ret;
}
#undef METHOD_NAME

#define METHOD_NAME "get_background_items"
static GList *
thunarx_python_object_get_background_items (ThunarxMenuProvider *provider,
											 GtkWidget *window,
											 ThunarxFileInfo *file)
{
	ThunarxPythonObject *object = (ThunarxPythonObject*)provider;
    GList *ret = NULL;
    PyObject *py_ret = NULL;
	PyGILState_STATE state = pyg_gil_state_ensure();                                    \
	
  	debug_enter();
	
	CHECK_METHOD_NAME(object->instance);

    py_ret = PyObject_CallMethod(object->instance, METHOD_PREFIX METHOD_NAME,
								 "(NN)",
								 pygobject_new((GObject *)window),
								 pygobject_new((GObject *)file));
								 
	HANDLE_RETVAL(py_ret);

	HANDLE_LIST(py_ret, ThunarxMenuItem, "thunarx.MenuItem");
	
 beach:
	Py_XDECREF(py_ret);
	pyg_gil_state_release(state);
    return ret;
}
#undef METHOD_NAME

#define METHOD_NAME "get_toolbar_items"
static GList *
thunarx_python_object_get_toolbar_items (ThunarxMenuProvider *provider,
										  GtkWidget *window,
										  ThunarxFileInfo *file)
{
	ThunarxPythonObject *object = (ThunarxPythonObject*)provider;
    GList *ret = NULL;
    PyObject *py_ret = NULL;
	PyGILState_STATE state = pyg_gil_state_ensure();                                    \
	
  	debug_enter();

	CHECK_METHOD_NAME(object->instance);

    py_ret = PyObject_CallMethod(object->instance, METHOD_PREFIX METHOD_NAME,
								 "(NN)",
								 pygobject_new((GObject *)window),
								 pygobject_new((GObject *)file));
	HANDLE_RETVAL(py_ret);

	HANDLE_LIST(py_ret, ThunarxMenuItem, "thunarx.MenuItem");
	
 beach:
	Py_XDECREF(py_ret);
	pyg_gil_state_release(state);
    return ret;
}
#undef METHOD_NAME

static void
thunarx_python_object_menu_provider_iface_init (ThunarxMenuProviderIface *iface)
{
	iface->get_background_items = thunarx_python_object_get_background_items;
	iface->get_toolbar_items = thunarx_python_object_get_toolbar_items;
	iface->get_file_items = thunarx_python_object_get_file_items;
}

#define METHOD_NAME "get_columns"
static GList *
thunarx_python_object_get_columns (ThunarxColumnProvider *provider)
{
	ThunarxPythonObject *object = (ThunarxPythonObject*)provider;
    GList *ret = NULL;
    PyObject *py_ret;
	PyGILState_STATE state = pyg_gil_state_ensure();                                    \

	debug_enter();
		
	CHECK_METHOD_NAME(object->instance);

    py_ret = PyObject_CallMethod(object->instance, METHOD_PREFIX METHOD_NAME,
								 NULL);

	HANDLE_RETVAL(py_ret);

	HANDLE_LIST(py_ret, ThunarxColumn, "thunarx.Column");
	
 beach:
	pyg_gil_state_release(state);
    return ret;
}
#undef METHOD_NAME

static void
thunarx_python_object_column_provider_iface_init (ThunarxColumnProviderIface *iface)
{
	iface->get_columns = thunarx_python_object_get_columns;
}


#define METHOD_NAME "cancel_update"
static void
thunarx_python_object_cancel_update (ThunarxInfoProvider *provider,
									  ThunarxOperationHandle *handle)
{
  	debug_enter();
}
#undef METHOD_NAME

#define METHOD_NAME "update_file_info"
static ThunarxOperationResult
thunarx_python_object_update_file_info (ThunarxInfoProvider *provider,
										 ThunarxFile *file,
										 GClosure *update_complete,
										 ThunarxOperationHandle **handle)
{
	ThunarxPythonObject *object = (ThunarxPythonObject*)provider;
    ThunarxOperationResult ret = NAUTILUS_OPERATION_COMPLETE;
    PyObject *py_ret = NULL;
	PyGILState_STATE state = pyg_gil_state_ensure();                                    \
	
  	debug_enter();

	CHECK_METHOD_NAME(object->instance);

    py_ret = PyObject_CallMethod(object->instance,
								 METHOD_PREFIX METHOD_NAME, "(N)",
								 pygobject_new((GObject*)file));
	HANDLE_RETVAL(py_ret);


	if (!PyInt_Check(py_ret)) {
		PyErr_SetString(PyExc_TypeError,
						METHOD_NAME " must return None or a int");
		goto beach;
	}

	ret = PyInt_AsLong(py_ret);
	
 beach:
	Py_XDECREF(py_ret);
	pyg_gil_state_release(state);
    return ret;
}
#undef METHOD_NAME

static void
thunarx_python_object_info_provider_iface_init (ThunarxInfoProviderIface *iface)
{
	iface->cancel_update = thunarx_python_object_cancel_update;
	iface->update_file_info = thunarx_python_object_update_file_info;
}

static void 
thunarx_python_object_instance_init (ThunarxPythonObject *object)
{
	ThunarxPythonObjectClass *class;
  	debug_enter();

	class = (ThunarxPythonObjectClass*)(((GTypeInstance*)object)->g_class);

	object->instance = PyObject_CallObject(class->type, NULL);
	if (object->instance == NULL)
		PyErr_Print();
}

static void
thunarx_python_object_finalize (GObject *object)
{
  	debug_enter();

	Py_DECREF(((ThunarxPythonObject *)object)->instance);
}

static void
thunarx_python_object_class_init (ThunarxPythonObjectClass *class,
								   gpointer class_data)
{
	debug_enter();

	parent_class = g_type_class_peek_parent (class);
	
	class->type = (PyObject*)class_data;
	
	G_OBJECT_CLASS (class)->finalize = thunarx_python_object_finalize;
}

GType 
thunarx_python_object_get_type (GTypeModule *module, 
								 PyObject *type)
{
	GTypeInfo *info;
	const char *type_name;
	GType gtype;
	  
	static const GInterfaceInfo property_page_provider_iface_info = {
		(GInterfaceInitFunc) thunarx_python_object_property_page_provider_iface_init,
		NULL,
		NULL
	};

	static const GInterfaceInfo location_widget_provider_iface_info = {
		(GInterfaceInitFunc) thunarx_python_object_location_widget_provider_iface_init,
		NULL,
		NULL
	};

	static const GInterfaceInfo menu_provider_iface_info = {
		(GInterfaceInitFunc) thunarx_python_object_menu_provider_iface_init,
		NULL,
		NULL
	};

	static const GInterfaceInfo column_provider_iface_info = {
		(GInterfaceInitFunc) thunarx_python_object_column_provider_iface_init,
		NULL,
		NULL
	};

	static const GInterfaceInfo info_provider_iface_info = {
		(GInterfaceInitFunc) thunarx_python_object_info_provider_iface_init,
		NULL,
		NULL
	};

	debug_enter_args("type=%s", PyString_AsString(PyObject_GetAttrString(type, "__name__")));
	info = g_new0 (GTypeInfo, 1);
	
	info->class_size = sizeof (ThunarxPythonObjectClass);
	info->class_init = (GClassInitFunc)thunarx_python_object_class_init;
	info->instance_size = sizeof (ThunarxPythonObject);
	info->instance_init = (GInstanceInitFunc)thunarx_python_object_instance_init;

	info->class_data = type;
	Py_INCREF(type);

	type_name = g_strdup_printf("%s+ThunarxPython",
								PyString_AsString(PyObject_GetAttrString(type, "__name__")));
		
	gtype = g_type_module_register_type (module, 
										 G_TYPE_OBJECT,
										 type_name,
										 info, 0);

	if (PyObject_IsSubclass(type, (PyObject*)&PyThunarxPropertyPageProvider_Type)) {
		g_type_module_add_interface (module, gtype, 
									 NAUTILUS_TYPE_PROPERTY_PAGE_PROVIDER,
									 &property_page_provider_iface_info);
	}

	if (PyObject_IsSubclass(type, (PyObject*)&PyThunarxLocationWidgetProvider_Type)) {
		g_type_module_add_interface (module, gtype,
									 NAUTILUS_TYPE_LOCATION_WIDGET_PROVIDER,
									 &location_widget_provider_iface_info);
	}
	
	if (PyObject_IsSubclass(type, (PyObject*)&PyThunarxMenuProvider_Type)) {
		g_type_module_add_interface (module, gtype, 
									 NAUTILUS_TYPE_MENU_PROVIDER,
									 &menu_provider_iface_info);
	}

	if (PyObject_IsSubclass(type, (PyObject*)&PyThunarxColumnProvider_Type)) {
		g_type_module_add_interface (module, gtype, 
									 NAUTILUS_TYPE_COLUMN_PROVIDER,
									 &column_provider_iface_info);
	}
	
	if (PyObject_IsSubclass(type, (PyObject*)&PyThunarxInfoProvider_Type)) {
		g_type_module_add_interface (module, gtype, 
									 NAUTILUS_TYPE_INFO_PROVIDER,
									 &info_provider_iface_info);
	}
	
	return gtype;
}
