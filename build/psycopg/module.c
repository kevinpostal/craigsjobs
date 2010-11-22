/*
 * Copyright (C) 2001 Michele Comitini <mcm@initd.net>
 * Copyright (C) 2001-2003 Federico Di Gregorio <fog@debian.org>
 *
 * This file is part of the psycopg module.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * module.c -- defines the module interface to DBAPI 2.0
 * $Id: module.c 554 2004-10-30 00:19:27Z fog $
 */

#ifdef _WIN32
#include "config32.h"
#else
#include "config.h"
#endif

#include "module.h"
#include "typeobj.h"
#include "typemod.h"

#include <string.h>

/**** MODULE FUNCTIONS ****/

/* psyco_connect() - create a new connection */

static char psyco_connect__doc__[] = 
"Initialize the connection object.\n"
"\n"
"    connect(dsn [,maxconn [,minconn]])\n"
"\n"
"'dsn' is data source name as string (e.g., \"host=myhost dbname=mydb\n"
"user=username password=mypass\"), 'maxconn' is the maximum number of\n"
"physical connections to posgresql (default is "MACRO_STR(MAXCONN)"), "
"'minconn' is the\n"
"minimum number of physical connections will be available for reuse\n"
"(must be minconn < maxconn).";

static PyObject *
psyco_connect(PyObject *self, PyObject *args, PyObject *keywds)
{
    PyObject *conn;
    int maxconn=MAXCONN, minconn=MINCONN, serialize=1, idsn=-1;
    char *dsn=NULL, *database=NULL, *user=NULL, *password=NULL,
         *host=NULL, *port=NULL, *sslmode=NULL;
    
    static char *kwlist[] = {"dsn", "database", "host", "port",
                             "user", "password", "sslmode", 
                             "maxconn", "minconn", "serialize", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|sssssssiii", kwlist,
                                     &dsn, &database, &host, &port,
                                     &user, &password, &sslmode,
                                     &maxconn, &minconn, &serialize)) {
        return NULL;
    }

    if (dsn == NULL) {
        int l = 36;  /* len("dbname= user= password= host= port=\0") */
        if (database) l += strlen(database);
        if (host) l += strlen(host);
        if (port) l += strlen(port);
        if (user) l += strlen(user);
        if (password) l += strlen(password);
        if (sslmode) l += strlen(sslmode);
        
        dsn = malloc(l*sizeof(char));
        if (dsn == NULL) {
            PyErr_SetString(InterfaceError, "dynamic dsn allocation failed");
            return NULL;
        }
        
        idsn = 0;
        if (database) {
            strcpy(&dsn[idsn], " dbname="); idsn += 8;
            strcpy(&dsn[idsn], database); idsn += strlen(database);
        }
        if (host) {
            strcpy(&dsn[idsn], " host="); idsn += 6;
            strcpy(&dsn[idsn], host); idsn += strlen(host);
        }
        if (port) {
            strcpy(&dsn[idsn], " port="); idsn += 6;
            strcpy(&dsn[idsn], port); idsn += strlen(port);
        }
        if (user) {
            strcpy(&dsn[idsn], " user="); idsn += 6;
            strcpy(&dsn[idsn], user); idsn += strlen(user);
        }
        if (password) {
            strcpy(&dsn[idsn], " password="); idsn += 10;
            strcpy(&dsn[idsn], password); idsn += strlen(password);
        }
        if (sslmode) {
            strcpy(&dsn[idsn], " sslmode="); idsn += 9;
            strcpy(&dsn[idsn], sslmode); idsn += strlen(sslmode);
        }

        if (idsn > 0) {
            dsn[idsn] = '\0';
            memmove(dsn, &dsn[1], idsn);
        }
        else {
            free(dsn);
            PyErr_SetString(InterfaceError, "missing dsn and no parameters");
            return NULL;
        }
    }
        
    Dprintf("psyco_connect(): dsn = '%s', serialize = %d\n", dsn, serialize);
    Dprintf("psyco_connect(): minconn = %d, maxconn = %d\n", minconn, maxconn);

    if (maxconn < 0 || minconn < 0 || maxconn < minconn) {
        PyErr_SetString(InterfaceError, "wrong value for maxconn/minconn");
        return NULL;
    }

    if (serialize != 0 && serialize != 1) {
        PyErr_SetString(InterfaceError, "wrong value for serialize");
        return NULL;
    }
    
    conn = (PyObject *)new_psyco_connobject(dsn, maxconn, minconn, serialize);
    if (idsn != -1) free(dsn);
    
    return conn;
}


/* psyco_register_type() - register a new type object with the type system */

static char psyco_register_type__doc__[] = "Register a new type object.";

static PyObject *
psyco_register_type(PyObject *self, PyObject *args, PyObject *keywds)
{
    PyObject *type;
    static char *kwlist[] = {"typeobj", NULL};
    
    
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "O!", kwlist,
                                     &psyco_DBAPITypeObject_Type, &type)) {
        return NULL;
    }

    psyco_add_type(type);
    Py_INCREF(Py_None);
    return Py_None;
}


/**** MODULE DEFINITION ****/

/* module methods list */

static PyMethodDef psycopgMethods[] = {
    {"connect", (PyCFunction)psyco_connect,
     METH_VARARGS|METH_KEYWORDS, psyco_connect__doc__},
    
    {"register_type", (PyCFunction)psyco_register_type,
     METH_VARARGS|METH_KEYWORDS, psyco_register_type__doc__},
    {"new_type", (PyCFunction)psyco_DBAPITypeObject_init,
     METH_VARARGS|METH_KEYWORDS},
    
    {"Date", (PyCFunction)psyco_Date, METH_VARARGS},
    {"Time", (PyCFunction)psyco_Time, METH_VARARGS},
    {"Timestamp", (PyCFunction)psyco_Timestamp, METH_VARARGS},
    {"DateFromTicks", (PyCFunction)psyco_DateFromTicks, METH_VARARGS},
    {"TimeFromTicks", (PyCFunction)psyco_TimeFromTicks, METH_VARARGS},
    {"TimestampFromTicks", (PyCFunction)psyco_TimestampFromTicks, METH_VARARGS},
    {"DateFromMx", (PyCFunction)psyco_DateFromMx, METH_VARARGS},
    {"TimeFromMx", (PyCFunction)psyco_TimeFromMx, METH_VARARGS},
    {"TimestampFromMx", (PyCFunction)psyco_TimestampFromMx, METH_VARARGS},
    {"Binary", (PyCFunction)psyco_Binary, METH_VARARGS},
    {"QuotedString", (PyCFunction)psyco_QuotedString, METH_VARARGS},
    {NULL,  NULL}
};



/**** initpsycopg() - module initialization, non-static ****/

/* global access to mxDateTime struct */
mxDateTimeModule_APIObject *mxDateTimeP;

void
initpsycopg(void)
{
	PyObject *m, *d;

    mxDateTime_ImportModuleAndAPI();
    mxDateTimeP = &mxDateTime;
    Dprintf("initpsycopg: mxDateTime module imported at %p\n", mxDateTimeP); 
    
    /* init module and grab module namespace (dictionary) */
	m = Py_InitModule("psycopg", psycopgMethods);
    d = PyModule_GetDict(m);
    Dprintf("initpsycopg: module initialized\n");
    
    /* DBAPI compliance module parameters */
    PyDict_SetItemString(d, "__version__",
                         PyString_FromString(PACKAGE_VERSION));
    PyDict_SetItemString(d, "apilevel", PyString_FromString(APILEVEL));
    PyDict_SetItemString(d, "threadsafety", PyInt_FromLong(THREADSAFETY));
    PyDict_SetItemString(d, "paramstyle", PyString_FromString(PARAMSTYLE));
    Dprintf("initpsycopg: parameters initialized\n");
 
    /* create the default DBAPITypeObject dictionary */
    psyco_init_types(d);
    Dprintf("initpsycopg: types initialized\n");
    
	/* exceptions of this module */
	Error = PyErr_NewException("psycopg.Error", PyExc_StandardError,NULL);
	PyDict_SetItemString(d, "Error", Error);

	Warning = PyErr_NewException("psycopg.Warning", PyExc_StandardError,NULL);
	PyDict_SetItemString(d, "Warning", Warning);

	/* subclasses of Error */
	InterfaceError = PyErr_NewException("psycopg.InterfaceError", Error, NULL);
	PyDict_SetItemString(d, "InterfaceError", InterfaceError);

	DatabaseError = PyErr_NewException("psycopg.DatabaseError", Error, NULL);
	PyDict_SetItemString(d, "DatabaseError", DatabaseError);

	/* subclasses of DatabaseError */
	InternalError =
        PyErr_NewException("psycopg.InternalError", DatabaseError, NULL);
	PyDict_SetItemString(d, "InternalError", InternalError);

	OperationalError =
        PyErr_NewException("psycopg.OperationalError", DatabaseError, NULL);
	PyDict_SetItemString(d, "OperationalError", OperationalError);

	ProgrammingError =
        PyErr_NewException("psycopg.ProgrammingError", DatabaseError, NULL);
	PyDict_SetItemString(d, "ProgrammingError", ProgrammingError);
    
	IntegrityError =
        PyErr_NewException("psycopg.IntegrityError", DatabaseError,NULL);
	PyDict_SetItemString(d, "IntegrityError", IntegrityError);

	DataError = PyErr_NewException("psycopg.DataError", DatabaseError, NULL);
	PyDict_SetItemString(d, "DataError", DataError);

	NotSupportedError =
        PyErr_NewException("psycopg.NotSupportedError", DatabaseError, NULL);
	PyDict_SetItemString(d, "NotSupportedError", NotSupportedError);

    Dprintf("initpsycopg: exceptions initialized\n");
}
