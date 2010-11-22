/*
 * Copyright (C) 2001 Michele Comitini <mcm@initd.net>
 * Copyright (C) 2001 Federico Di Gregorio <fog@debian.org>
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
 * connection.c -- defines the connection object DBAPI 2.0
 * $Id: connection.c 884 2005-09-12 09:20:17Z fog $
 */

#include "module.h"
#include <assert.h>

/**** UTILITY FUNCTIONS ****/

/* curs_closeall() - close all the cursors */
static void
curs_closeall(connobject *self)
{
    int len, i;
    PyObject *tmpobj;
    PyObject *cursors = self->cursors;

    pthread_mutex_lock(&(self->lock));

    /* close all the children cursors */
    len = PyList_Size(cursors);
    for (i = 0; i < len; i++) {
        tmpobj = PyList_GetItem(cursors, i);
        assert(tmpobj);
        ((cursobject *)tmpobj)->closed = 1;
    }
    pthread_mutex_unlock(&(self->lock));
}


typedef struct {
    cursobject *cursor;
    char *errmsg;
} doall_state_t;

/* _curs_doall() - execute an operation (commit or rollback) on all the cursors 
 * 
 * returns NULL on success, a python dictionary if at least one cursor failed.
 * the dictionary maps key: cursor/cursobject to value:
 * error/PyString.
 * Note: in case the dictionary could not be created (out of memory e.g.),
 *       Py_None is returned instead.
 */
static PyObject*
_curs_doall(connobject *self, int (*operation)(cursobject *) )
{
    int len, i, has_errors = 0;
    cursobject *cursor;

    doall_state_t *cursors = NULL;
    PyObject* errs = NULL;

    Dprintf("curs_doall: acquiring lock\n");
    pthread_mutex_lock(&(self->lock));
    Dprintf("curs_doall: lock acquired\n");

    /* collect all the cursors, so we can use them while not holding the GIL
     * We keep a reference to the cursors, in case the self->cursors changes
     * during the call. */
    len = PyList_Size(self->cursors);
    cursors = (doall_state_t *)malloc(len * sizeof (doall_state_t));
    if (!cursors) {
        pthread_mutex_unlock(&(self->lock));
        Dprintf("curs_doall: lock released\n");
        return PyErr_NoMemory();
    }
    for (i = 0; i < len; i++) {
        cursors[i].cursor = (cursobject *)PyList_GetItem(self->cursors, i);
        assert(cursors[i].cursor);
        Py_INCREF(cursors[i].cursor);
        cursors[i].errmsg = NULL;
    }

    Py_BEGIN_ALLOW_THREADS;
    
    Dprintf("curs_doall: %d cursors\n", len);
    
    /* acquire all the required locks */
    for (i = 0; i < len; i++) {
        cursor = cursors[i].cursor;
        Dprintf("curs_doall: lock/iterating on %p\n", cursor);
        if (cursor->keeper->status == KEEPER_BEGIN
            && cursor->isolation_level > 0){
            pthread_mutex_lock(&(cursor->keeper->lock));
            if (cursor->keeper->status == KEEPER_BEGIN) {
                cursor->keeper->status = KEEPER_CONN_LOCK;
                Dprintf("curs_doall: acquired lock on keeper %p cursor %p\n",
                        cursor->keeper, cursor);
            }
            else {
                pthread_mutex_unlock(&(cursor->keeper->lock));
            }
        }
    }

    /* does all the operations */
    for (i = 0; i < len; i++) {
        int status = 0;

        cursor = cursors[i].cursor;
        Dprintf("curs_doall: iterating on %p\n", cursor);
        if (cursor->keeper->status == KEEPER_CONN_LOCK) {
            Dprintf("curs_doall: operating on cursor %p\n", cursor);
            cursor->keeper->status = KEEPER_BEGIN;
            status = (*operation)(cursor);
            if (status == -1) {
                has_errors = 1;
                if (cursor->critical) {
                    cursors[i].errmsg = strdup(cursor->critical);
                }
            }
            cursor->keeper->status = KEEPER_CONN_READY;
        }
    }

    /* unlocks all the connections */
    for (i = 0; i < len; i++) {
        cursor = cursors[i].cursor;
        if (cursor->keeper->status == KEEPER_CONN_READY) {
            pthread_mutex_unlock(&(cursor->keeper->lock));
            cursor->keeper->status = KEEPER_READY;
            Dprintf("curs_doall: released lock on keeper %p\n",
                    cursor->keeper);
        }
    }

    pthread_mutex_unlock(&(self->lock));
    Dprintf("curs_doall: lock released\n");
    Py_END_ALLOW_THREADS;

    /* if an error occurred, set up the error dictionary (or set errs to
     * None if we can't create the dictionary). */
    if (has_errors) {
        errs = PyDict_New();
        if (errs) {
            for (i = 0; i < len; i++) {
                if (cursors[i].errmsg != NULL) {
                    PyObject *str = PyString_FromString(cursors[i].errmsg);
                    PyDict_SetItem(errs, (PyObject *)cursors[i].cursor, str);
                    Py_XDECREF(str);
                }
            }
        } else {
	  errs = Py_None;
	  Py_INCREF(errs);
	}
    }

    /* clean up the state array */
    for (i = 0; i < len; i++) {
        Py_DECREF(cursors[i].cursor);
        if (cursors[i].errmsg)
            free(cursors[i].errmsg);
    }
    free(cursors);

    /* errs will be NULL if has_errors is False */
    return errs;
}


/* curs_rollbackall() execute a rollback on all the cursors
 * returns NULL on success, a python dictionary if at least one cursor failed
 * to rollback; the dictionary maps key: cursor/cursobject to value:
 * error/PyString.
 * Note: in case the dictionary could not be created (out of memory e.g.),
 *       Py_None is returned instead.
 */
static PyObject*
curs_rollbackall(connobject *self)
{
    return _curs_doall(self, abort_pgconn);
}


/* curs_commitall() execute a commit on all the cursors
 * returns NULL on success, a python dictionary if at least one cursor failed
 * to rollback; the dictionary maps key: cursor/cursobject to value:
 * error/PyString.
 * Note: in case the dictionary could not be created (out of memory e.g.),
 *       Py_None is returned instead.
 */
static PyObject*
curs_commitall(connobject *self)
{
    return _curs_doall(self, commit_pgconn);
}


/**** CONNECTION METHODS *****/

/* psyco_conn_close() - close the connection */

static char psyco_conn_close__doc__[] = "Closes the connection.";

static void
_psyco_conn_close(connobject *self)
{
    int len, i;
    PyObject *tmpobj = NULL;
    connkeeper *keeper;
    
    Dprintf("_psyco_conn_close(): closing all cursors\n");
    curs_closeall(self);

    /* orphans all the children cursors but do NOT destroy them (note that
       we need to lock the keepers used by the cursors before destroying the
       connection, else we risk to be inside an execute while we set pgconn
       to NULL) */
    assert(self->cursors != NULL);
    len = PyList_Size(self->cursors);
    Dprintf("_psyco_conn_close(): len(self->cursors) = %d\n", len);
    for (i = len-1; i >= 0; i--) {
        tmpobj = PyList_GetItem(self->cursors, i);
        assert(tmpobj);
        Dprintf("_psyco_conn_close(): detaching cursor at %p: refcnt = %d\n",
                tmpobj, tmpobj->ob_refcnt);
        Py_INCREF(tmpobj);
        PySequence_DelItem(self->cursors, i);
        dispose_pgconn((cursobject *)tmpobj);
        ((cursobject *)tmpobj)->conn = NULL; /* orphaned */
        Dprintf("_psyco_conn_close(): cursor at %p detached: refcnt = %d\n",
                tmpobj, tmpobj->ob_refcnt);
    }

    /* close all the open postgresql connections */
    len = PyList_Size(self->avail_conn);
    Dprintf("_psyco_conn_close(): len(self->avail_conn) = %d\n", len);
    for (i = len-1; i >= 0; i--) {
        tmpobj = PyList_GetItem(self->avail_conn, i);
        assert(tmpobj);
        Py_INCREF(tmpobj);
        if ((keeper = (connkeeper *)PyCObject_AsVoidPtr(tmpobj))) {
            Dprintf("_psyco_conn_close(): destroying avail_conn[%i] at %p\n",
                    i, keeper);
            PQfinish(keeper->pgconn);
            pthread_mutex_destroy(&(keeper->lock));
            free(keeper);
        }
        PySequence_DelItem(self->avail_conn, i);
        Py_DECREF(tmpobj);
    }
    
    Py_DECREF(self->cursors);
    Py_DECREF(self->avail_conn);
    self->cursors = NULL;
    self->avail_conn = NULL;

    /* orphan default cursor and destroy it (closing the last connection to the
       database) */
    Dprintf("_psyco_conn_close(): killing stdmanager\n");
    self->stdmanager->conn = NULL;
    Py_DECREF(self->stdmanager);
    self->stdmanager = NULL;
}

static PyObject *
psyco_conn_close(connobject *self, PyObject *args)
{
    EXC_IFCLOSED(self);
    PARSEARGS(args);

    /* from now on the connection is considered closed */
    self->closed = 1;  
    _psyco_conn_close(self);
    Dprintf("psyco_conn_close(): connection closed\n");
    Py_INCREF(Py_None);
    return Py_None;
}


/* psyco_conn_commit() - commit the connection (commit all the cursors) */

static char psyco_conn_commit__doc__[] =
"Commit any pending transaction to the database."
"Raises DatabaseError in case some cursors fail to commit their changes "
"--in that case, the value attached to the exception is a dictionary mapping "
"the cursors that failed with the corresponding error string.";

static PyObject *
psyco_conn_commit(connobject *self,PyObject *args)
{
    PyObject* errs;
   
    EXC_IFCLOSED(self);
    PARSEARGS(args);

    errs = curs_commitall(self);

    if (errs) {
        cursobject* cursor;
        PyObject *key, *value;
        int pos = 0;
        Dprintf("psyco_conn_commit: curs_commitall() failed\n");
        PyErr_SetObject(DatabaseError, errs);
        if (errs != Py_None) {
            /* Now that the errors have been taken into account, make sure
               that the cursors that were marked as critical can be used again
               in the future. There is no need to set the keeper's status,
               this has been done in curs_commitall() */
            while (PyDict_Next(errs, &pos, &key, &value)) {
            	cursor = (cursobject*)key;
            	if (cursor->critical) free(cursor->critical);
            	cursor->critical = NULL;
            }
        }
        Py_DECREF(errs);
        return NULL;
    }
    else {
        Dprintf("psyco_conn_commit: commit executed\n");
        Py_INCREF(Py_None);
        return Py_None;
    }
}


/* psyco_conn_rollback() - rollback the connection (rollback on all cursors) */

static char psyco_conn_rollback__doc__[] = 
"Causes the database to roll back to the start of any pending transaction."
"Raises DatabaseError in case some cursors fail to rollback their changes "
"--in that case, the value attached to the exception is a dictionary mapping "
"the cursors that failed with the corresponding error string.";

static PyObject *
psyco_conn_rollback(connobject *self,	PyObject *args)
{
    PyObject* errs;
   
    EXC_IFCLOSED(self);
    PARSEARGS(args);

    errs = curs_rollbackall(self);
    
    if (errs) {
        cursobject* cursor;
        PyObject *key, *value;
        int pos = 0;
        Dprintf("psyco_conn_rollback: curs_rollbackall() failed\n");
        PyErr_SetObject(DatabaseError, errs);
        if (errs!=Py_None) {
            /* Now that the errors have been taken into account, make sure
               that the cursors that were marked as critical can be used again
               in the future. There is no need to set the keeper's status,
               this has been done in curs_rollbackall() */
            while (PyDict_Next(errs, &pos, &key, &value)) {
            	cursor = (cursobject*)key;
            	if (cursor->critical) free(cursor->critical);
            	cursor->critical = NULL;
            }
        }
        Py_DECREF(errs);
        return NULL;
    }
    else {
        Dprintf("psyco_conn_rollback(): rollback executed\n");
        Py_INCREF(Py_None);
        return Py_None;
    }
}

/* psyco_conn_cursor() - create a new cursor */

static char psyco_conn_cursor__doc__[] = 
"Return a new Cursor Object using the connection.";

static PyObject *
psyco_conn_cursor(connobject *self, PyObject *args)
{
    char *name = NULL;
    connkeeper *keeper = NULL;
    PyObject *obj;
    
    if (!PyArg_ParseTuple(args, "|s", &name)) {
        return NULL;
    }
    
    EXC_IFCLOSED(self);

    Dprintf("psyco_conn_cursor(): conn = %p\n", self);
    Dprintf("psyco_conn_cursor(): serialize = %d, keeper = %p\n",
            self->serialize, self->stdmanager->keeper);
    
    if (self->serialize == 0 || name) {
        keeper = NULL;
    }
    else {
        /* here we need to bump up the refcount before passing the keeper
           to the cursor for initialization, because another thread can
           dispose the keeper in the meantime */
        keeper = self->stdmanager->keeper;
        pthread_mutex_lock(&(keeper->lock));
        keeper->refcnt++;
        pthread_mutex_unlock(&(keeper->lock));
    }
    
    obj = (PyObject *)new_psyco_cursobject(self, keeper);
    return obj; 
}


/* psyco_conn_set_isolation_level() - set isolation level for this connection
   and all derived cursors */

static char psyco_conn_set_isolation_level__doc__[] = 
"Switch connection (and derived cursors) isolation level.";

static void
_psyco_conn_set_isolation_level(connobject *self, int level)
{
    PyObject *tmpobj;
    int i, len;
    
    /* save the value */
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    self->isolation_level = level;

    /* make sure the list of cursors will not change by other threads while
       iterating (curs_switch_isolation_level() allows other threads to run) */
    pthread_mutex_lock(&(self->lock));

    /* call set_isolation_level on all the cursors to change their status */
    len = PyList_Size(self->cursors);
    for (i = 0; i < len; i++) {
        tmpobj = PyList_GetItem(self->cursors, i);
        assert(tmpobj);
        Py_INCREF(tmpobj);
        curs_switch_isolation_level((cursobject *)tmpobj, level);
        Py_DECREF(tmpobj);
    }
    pthread_mutex_unlock(&(self->lock));
}

static PyObject *
psyco_conn_set_isolation_level(connobject *self, PyObject *args)
{
    long int level;
    
    if (!PyArg_ParseTuple(args, "l", &level)) {
        return NULL;
    }

    EXC_IFCLOSED(self);

    _psyco_conn_set_isolation_level(self, level);
    
    Py_INCREF(Py_None);
    return Py_None;
}

/* psyco_conn_autocommit() - put connection (and all derived cursors)
   in autocommit mode */

static char psyco_conn_autocommit__doc__[] = 
"Switch connection (and derived cursors) to/from autocommit mode.";

static PyObject *
psyco_conn_autocommit(connobject *self, PyObject *args)
{
    long int ac = 1; /* the default is to set autocommit on */
    int isolation_level = 0;
    
    if (!PyArg_ParseTuple(args, "|l", &ac)) {
        return NULL;
    }

    if (ac == 0) isolation_level = 2;

    EXC_IFCLOSED(self);

    _psyco_conn_set_isolation_level(self, isolation_level);

    Py_INCREF(Py_None);
    return Py_None;
}

/* psyco_conn_serialize() - switch on and off cursor serialization */

static char psyco_conn_serialize__doc__[] = 
"Switch on and off cursor serialization.";

static PyObject *
psyco_conn_serialize(connobject *self, PyObject *args)
{
    long int se = 1; /* the default is to set serialize on */
    
    if (!PyArg_ParseTuple(args, "|l", &se)) {
        return NULL;
    }

    EXC_IFCLOSED(self);

    /* save the value */
    self->serialize = (int)se;

    Py_INCREF(Py_None);
    return Py_None;
}


/**** CONNECTION OBJECT DEFINITION ****/

/* object methods list */

static struct PyMethodDef psyco_conn_methods[] = {
    {"close", (PyCFunction)psyco_conn_close,
     METH_VARARGS, psyco_conn_close__doc__},
    {"commit", (PyCFunction)psyco_conn_commit,
     METH_VARARGS, psyco_conn_commit__doc__},
    {"rollback", (PyCFunction)psyco_conn_rollback,
     METH_VARARGS, psyco_conn_rollback__doc__},
    {"cursor", (PyCFunction)psyco_conn_cursor,
     METH_VARARGS, psyco_conn_cursor__doc__},
    {"autocommit", (PyCFunction)psyco_conn_autocommit,
     METH_VARARGS, psyco_conn_autocommit__doc__},
    {"set_isolation_level", (PyCFunction)psyco_conn_set_isolation_level,
     METH_VARARGS, psyco_conn_set_isolation_level__doc__},   
    {"serialize", (PyCFunction)psyco_conn_serialize,
     METH_VARARGS, psyco_conn_serialize__doc__},
    {NULL, NULL}
};


/* object member list */

#define OFFSETOF(x) offsetof(connobject, x)

static struct memberlist psyco_conn_memberlist[] = {
    { "cursors", T_OBJECT, OFFSETOF(cursors), RO},
    { "maxconn", T_INT, OFFSETOF(maxconn), RO},
    { "minconn", T_INT, OFFSETOF(minconn), RO},
    {NULL}
};


/* the python object interface for the connection object */

static PyObject *
psyco_conn_getattr(connobject *self, char *name)
{
    PyObject *rv;
	
    rv = PyMember_Get((char *)self, psyco_conn_memberlist, name);
    if (rv) return rv;
    PyErr_Clear();
    return Py_FindMethod(psyco_conn_methods, (PyObject *)self, name);
}

static int
psyco_conn_setattr(connobject *self, char *name, PyObject *v)
{
    if (v == NULL) {
        PyErr_SetString(PyExc_AttributeError, "cannot delete attribute");
        return -1;
    }
    return PyMember_Set((char *)self, psyco_conn_memberlist, name, v);
}

static void
psyco_conn_destroy(connobject *self)
{
    if (self->closed == 0) _psyco_conn_close(self);
    pthread_mutex_destroy(&(self->lock));
    free(self->dsn);
    PyObject_Del(self);
    Dprintf("psyco_conn_destroy(): connobject at %p destroyed\n", self);
}


static char Conntype__doc__[] = "Connections Object.";

static PyTypeObject Conntype = {
#if !defined(_WIN32) && !defined(__CYGWIN__)
    PyObject_HEAD_INIT(&PyType_Type)
#else
    PyObject_HEAD_INIT(NULL)
#endif
    0,				        /*ob_size*/
    "connection",			/*tp_name*/
    sizeof(connobject),		/*tp_basicsize*/
    0,				        /*tp_itemsize*/

    /* methods */
    (destructor)psyco_conn_destroy,   /*tp_dealloc*/
    (printfunc)0,		             /*tp_print*/
    (getattrfunc)psyco_conn_getattr,	 /*tp_getattr*/
    (setattrfunc)psyco_conn_setattr,	 /*tp_setattr*/
    (cmpfunc)0,		                 /*tp_compare*/
    (reprfunc)0,		             /*tp_repr*/
    0,			                     /*tp_as_number*/
    0,                               /*tp_as_sequence*/
    0,		                         /*tp_as_mapping*/
    (hashfunc)0,                     /*tp_hash*/
    (ternaryfunc)0,		             /*tp_call*/
    (reprfunc)0,                     /*tp_str*/
  
    /* Space for future expansion */
    0L,0L,0L,0L,
    Conntype__doc__ /* Documentation string */
};


/* the C constructor for connection objects */

connobject *
new_psyco_connobject(char *dsn, int maxconn, int minconn, int serialize)
{
    connobject *self;

    Dprintf("new_psyco_connobject(): creating new connection\n");

#if defined(_WIN32) || defined(__CYGWIN__)
    /* For MSVC workaround */
    Conntype.ob_type = &PyType_Type;
#endif
    
    self = PyObject_NEW(connobject, &Conntype);
    if (self == NULL) return NULL;

    pthread_mutex_init(&(self->lock), NULL);
    self->dsn = strdup(dsn);
    self->maxconn = maxconn;       
    self->minconn = minconn;
    self->cursors = PyList_New(0);
    self->avail_conn = PyList_New(0);
    self->closed = 0;
    self->isolation_level = 2;
    self->serialize = serialize;
    
    /* allocate default manager thread and keeper */
    self->stdmanager = new_psyco_cursobject(self, NULL);

    /* error checking done good */
    if (self->stdmanager == NULL || self->cursors == NULL
        || self->avail_conn == NULL) {
        Py_XDECREF(self->cursors);
        Py_XDECREF(self->avail_conn);
        Py_XDECREF(self->stdmanager);
        pthread_mutex_destroy(&(self->lock));
        PyObject_Del(self);
        return NULL;
    }

    Dprintf("new_psyco_connobject(): created connobject at %p, refcnt = %d\n",
            self, self->ob_refcnt);
    Dprintf("new_psyco_connobject(): stdmanager = %p, stdkeeper = %p\n",
            self->stdmanager, self->stdmanager->keeper);
    return self;
}




