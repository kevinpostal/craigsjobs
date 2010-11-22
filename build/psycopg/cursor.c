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
 * cursor.c - defines the cursor object DBAPI 2.0
 * $Id: cursor.c 826 2005-07-16 19:04:16Z fog $
 */

#ifdef _WIN32
#include "config32.h"
#else
#include "config.h"
#endif

#include "module.h"
#include "typemod.h"
#include <assert.h>

/* with some versions of postgres we need to include postgres.h to
   get the correct definition for InvalidOid */
#ifndef InvalidOid
#include <postgres.h>
#endif

/* include pgtypes.h to get right types #defines */
#include "pgtypes.h"


/**** UTILITY FUNCTIONS (callable from C only) ****/

/* pgconn_notice_*() - callbacks to process NOTICEs  */

void
pgconn_notice_callback(void *args, const char *message)
{
    if (strncmp(message, "ERROR", 5) == 0)
        PyErr_SetString(ProgrammingError, message);
    Dprintf("%s\n", message);
}


/* pgconn_set_critical() - manage critical errors
 *
 * this function is invoked when a PQexec() call returns NULL, meaning a
 * critical condition like out of memory or lost connection. it save the
 * error message and mark the cursor as 'wanting cleanup'.
 *
 * this function does not call Py_*_ALLOW_THREADS macros
 */
static void
pgconn_set_critical(cursobject *self)
{
    self->critical = strdup(PQerrorMessage(self->pgconn));
}

/* _psyco_curs_getout() - move the cursor out of the cursor list
 *
 * should be called while holding a lock to the connection
 * the connection should be valid
 */
static void
_psyco_curs_getout(cursobject *self)
{
    PyObject *tmpobj;
    int len, i;   

    if ((len = PyList_Size(self->conn->cursors)) > 0) {
        for (i = 0; i < len; i++) {
            tmpobj = PyList_GET_ITEM(self->conn->cursors, i);
            assert(tmpobj);
            if (self == (cursobject *)tmpobj) {
                Dprintf("_psyco_curs_getout: found myself in cursor list\n");
                PySequence_DelItem(self->conn->cursors, i);
                Dprintf("_psyco_curs_getout: cursor at %p removed from "
                        "list, refcnt = %d\n", tmpobj, tmpobj->ob_refcnt);
                break;
            }
        }
    } 
}

/* dispose_pgconn() - return the posgresql connection to the pool
 *
 * this function operate on the keeper only if the current cursor is the
 *   only owner of the keeper and after the cursor has been removed from
 *   the connection list of cursors, so it is safe to proceed without locking
 *   the keeper
 * this function locks the connection to access the avail_conn list
 * this function enters an ALLOW_THREADS wrapper
 */
int
dispose_pgconn(cursobject *self)
{
    PyObject *cpgconn;
    int refcnt, result;
    
    /* if we don't have a connection, print a warning and return */
    if (!self->keeper) {
        Dprintf("dispose_pgconn: self->keeper == NULL, self = %p\n", self);
        return 0;
    }

    Dprintf("dispose_pgconn: keeper->refcnt = %d\n", self->keeper->refcnt);
    pthread_mutex_lock(&(self->keeper->lock));
    refcnt = --self->keeper->refcnt;    

    /* other cursors are using this keeper, bails out, but first set the
       keeper to NULL to avoid decrementing the refcnt of the keeper two times
       in a row */
    if (refcnt > 0) {
        pthread_mutex_unlock(&(self->keeper->lock));
        self->keeper = NULL;
        return 0;
    }
    Dprintf("dispose_pgconn: keeper at %p is unused\n", self->keeper);
    
    /* try to rollback, if the connection return an error we simply free
       the keeper and go on (we are the only thread, we don't need to lock
       the keeper) */
    Py_BEGIN_ALLOW_THREADS;
    result = abort_pgconn(self);
    pthread_mutex_unlock(&(self->keeper->lock));
    Py_END_ALLOW_THREADS;

    /* now, if we can't get an hold on cursor's connection, that means that
       this cursor was orphaned by the garbage system reclaiming the connection
       object. we don't have a place to put the pgconn back, so we free it */
    if (result < 0 || !self->conn || !self->conn->avail_conn
        || self->critical != NULL) {
        Dprintf("dispose_pgconn: can't find connection or error: "
                "calling PQfinish()\n");
        PQfinish(self->pgconn);
        pthread_mutex_destroy(&(self->keeper->lock));
        free(self->keeper);
    }
    else {
        pthread_mutex_lock(&(self->conn->lock));
        self->keeper->status = KEEPER_READY;
        cpgconn = PyCObject_FromVoidPtr((void *)self->keeper, NULL);
        Dprintf("dispose_pgconn: appending keeper PyCObject to the list\n");
        PyList_Append(self->conn->avail_conn, cpgconn);
        Dprintf("dispose_pgconn: cpgconn->refcnt = %d\n",
                cpgconn->ob_refcnt);
        Py_DECREF(cpgconn);
        pthread_mutex_unlock(&(self->conn->lock));
    }
    /* clean the cursor before putting it to sleep */
    self->keeper = NULL;
    return 0;
}

/* _psyco_curs_destroy() - destroy the cursor and its data
 *
 * this function locks the connection
 * this function does not need to lock the keeper because we act on it only
 *   _after_ removing ourselves from the cursor list and only if we are the
 *   last cursor using the keeper
 */

static int
_psyco_curs_destroy(cursobject *self)
{
    int result = 0;
    
    /* from now on this cursor will act as closed */
    Dprintf("_psyco_curs_destroy: closing cursor at %p\n", self);
    self->closed = 1;

    /* remove the cursor from the connection list */
    if (self->conn) {
        pthread_mutex_lock(&(self->conn->lock));
        _psyco_curs_getout(self);
        pthread_mutex_unlock(&(self->conn->lock));
    }

    /* dispose connection, if possible */
    if (dispose_pgconn(self)) result = -1;
    
    /* destroy cursor information */
    IFCLEARPGRES(self->pgres);
    Py_XDECREF(self->description);
    Py_INCREF(Py_None);
    self->description = Py_None;
    Py_XDECREF(self->status);
    Py_INCREF(Py_None);
    self->status = Py_None;
    Py_XDECREF(self->casts);
    self->casts = NULL;
    if (self->notice) free(self->notice);
    self->notice = NULL;
    if (self->critical) free(self->critical);
    self->critical = NULL;

    Dprintf("_psyco_curs_destroy: cursor at %p closed\n", self);
    return result;
}

static char psyco_curs_close__doc__[] = "Closes the cursor.";

static PyObject *
psyco_curs_close(cursobject *self, PyObject *args)
{
    PARSEARGS(args);
    EXC_IFCLOSED(self);
    IFCLEARPGRES(self->pgres);

    Dprintf("psyco_curs_close: closing cursor at %p\n", self);
    self->closed = 1;

    Py_INCREF(Py_None);
    return Py_None;
}

/* pgconn_resolve_critical() - manage critical errors
 *
 * this function is called when the user tries an operation on a cursor marked
 * by a critical error.  it simply closes the cursor and tries to finish the
 * connection to avoid memory leaks.
 *
 * this function does not call Py_*_ALLOW_THREADS macros
 */

static PyObject *
pgconn_resolve_critical(cursobject *self)
{

    if (self->critical) {
        Dprintf("pgconn_resolve_critical: error = %s\n", self->critical);
        PyErr_SetString(OperationalError, self->critical);
        psyco_curs_close(self, NULL);
        Dprintf("pgconn_resolve_critical: resolved\n");
    }
    return NULL;
}


/* commit_pgconn() - try to  commit the transaction
 *
 * this function does not call Py_*_ALLOW_THREADS macros
 * this function does not lock the keeper and should be called while
 *   holding a lock on it
 */
int
commit_pgconn(cursobject *self)
{
    const char *query = "END";
    int pgstatus, retvalue = -1;
    PGresult *pgres = NULL;
    
    /* if in autocommit mode does nothing */
    Dprintf("commit_pgconn: pgconn = %p, level = %d, status = %d\n",
            self->pgconn, self->isolation_level, self->keeper->status);
    if (self->isolation_level == 0 || self->keeper->status != KEEPER_BEGIN)
        return 0;
    
    assert(self->pgconn);
    
    pgres = PQexec(self->pgconn, query);
    if (pgres == NULL) {
        pgconn_set_critical(self);
        goto cleanup;
    }
    Dprintf("commit_pgconn: query executed\n");
    
    pgstatus = PQresultStatus(pgres);
    if (pgstatus != PGRES_COMMAND_OK ) {
        Dprintf("commit_pgconn: result is NOT OK\n");
        pgconn_set_critical(self);
        goto cleanup;
    }
    Dprintf("commit_pgconn: result is OK\n");

    /* all went well, we can set retvalue to 0 (i.e., no error) */
    retvalue = 0;
    self->keeper->status = KEEPER_READY;
    
 cleanup:
    IFCLEARPGRES(pgres);
    return retvalue;
}


/* begin_pgconn() - try to open the transaction
 *
 * this function does not call Py_*_ALLOW_THREADS macros
 * this function does not lock the keeper and should be called while
 *   holding a lock on it
*/
int
begin_pgconn(cursobject *self)
{
    const char *query[] = {
        NULL,
        "BEGIN; SET TRANSACTION ISOLATION LEVEL READ COMMITTED",
        "BEGIN; SET TRANSACTION ISOLATION LEVEL SERIALIZABLE",
        "BEGIN; SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"
    };

    int pgstatus, retvalue = -1;
    PGresult *pgres = NULL;

    /* if in autocommit mode does nothing */
    Dprintf("begin_pgconn: pgconn = %p, level = %d, status = %d\n",
            self->pgconn, self->isolation_level, self->keeper->status);
    if (self->isolation_level == 0 || self->keeper->status != KEEPER_READY)
        return 0;
    
    assert(self->pgconn);

    pgres = PQexec(self->pgconn, query[self->isolation_level]);
    if (pgres == NULL) {
        pgconn_set_critical(self);
        goto cleanup;
    }
    Dprintf("begin_pgconn: query executed\n");

    pgstatus = PQresultStatus(pgres);
    if (pgstatus != PGRES_COMMAND_OK ) {
        Dprintf("begin_pgconn: result is NOT OK\n");
        pgconn_set_critical(self);
        goto cleanup;
    }
    Dprintf("begin_pgconn: result is OK\n");
    
    /* all went well, we can set retvalue to 0 (i.e., no error) */
    retvalue = 0;
    self->keeper->status = KEEPER_BEGIN;

 cleanup:
    IFCLEARPGRES(pgres);
    return retvalue;
}


/* abort_pgconn() - try to abort the curent transaction
 *
 * this function does not call Py_*_ALLOW_THREADS macros
 * this function does not lock the keeper and should be called while
 *   holding a lock on it
*/
int
abort_pgconn(cursobject *self)
{
    const char *query = "ABORT";
    int pgstatus, retvalue = -1;
    PGresult *pgres = NULL;

    /* if in autocommit mode does nothing */
    Dprintf("abort_pgconn: pgconn = %p, level = %d, status = %d\n",
            self->pgconn, self->isolation_level, self->keeper->status);
    if (self->isolation_level == 0 || self->keeper->status != KEEPER_BEGIN)
        return 0;

    assert(self->pgconn);
    
    pgres = PQexec(self->pgconn, query);
    if (pgres == NULL) {
        pgconn_set_critical(self);
        goto cleanup;
    }
    Dprintf("abort_pgconn: query executed\n");
    
    pgstatus = PQresultStatus(pgres);
    if (pgstatus != PGRES_COMMAND_OK ) {
        Dprintf("abort_pgconn: result is NOT OK\n");
        pgconn_set_critical(self);
        PQreset(self->pgconn);
        goto cleanup;
    }
    Dprintf("abort_pgconn: result is OK\n");

    /* all went well, we can set retvalue to 0 (i.e., no error) */
    retvalue = 0;
    self->keeper->status = KEEPER_READY;
    
 cleanup:
    IFCLEARPGRES(pgres);
    return retvalue;
}


/* alloc_keeper() - allocate a new connection
 *
 * this function is used by both request_pgconn() and new_psyco_connobject()
 * this function can be called without any lock because operates on a new
 *   keeper object, not yet in any list and so not accessible by other threads
 * this function enters an ALLOW_THREADS wrapper
*/
connkeeper *
alloc_keeper(connobject *conn)
{
    PGconn *pgconn;
    PGresult *pgres;
    connkeeper *keeper;
    const char *datestyle = "SET DATESTYLE TO 'ISO'";
    
    Dprintf("alloc_keeper: opening new postgresql connection\n");
    Dprintf("alloc_keeper: dsn = %s (%p)\n", conn->dsn, conn->dsn);

    pgconn = PQconnectdb(conn->dsn);
        
    Dprintf("alloc_keeper: new posgresql connection at %p\n", pgconn);
        
    if (pgconn == NULL)
    {
        Dprintf("alloc_keeper: PQconnectdb(%s) failed\n", conn->dsn);
        PyErr_SetString(OperationalError, "PQconnectdb() failed");
        return NULL;
    }
    else if (PQstatus(pgconn) == CONNECTION_BAD)
    {
        Dprintf("alloc_keeper: PQconnectdb(%s) returned BAD\n", conn->dsn);
        PyErr_SetString(OperationalError, PQerrorMessage(pgconn));
        PQfinish(pgconn);
        return NULL;
    }
    
    /* sets the notice processor callback */
    PQsetNoticeProcessor(pgconn, pgconn_notice_callback, (void*)conn);

#if POSTGRESQL_MAJOR >= 8 || POSTGRESQL_MINOR >= 4   
    Dprintf("alloc_keeper: connection protocol version: %d\n",
            PQprotocolVersion(pgconn));
#endif
    
    Dprintf("alloc_keeper: setting datestyle to iso\n");
    pgres = PQexec(pgconn, datestyle);
    Dprintf("alloc_keeper: datestyle query executed\n");
    
    if (pgres == NULL || PQresultStatus(pgres) != PGRES_COMMAND_OK ) {
        Dprintf("alloc_keeper: error while setting datestyle to iso\n");
        PyErr_SetString(OperationalError, "can't set datestyle to ISO");
        PQfinish(pgconn);
        IFCLEARPGRES(pgres);
        return NULL;
    }

    CLEARPGRES(pgres);

    /* allocate the new keeper and initialize it */
    keeper = (connkeeper *)calloc(1, sizeof(connkeeper));
    keeper->pgconn = pgconn;
    pthread_mutex_init(&(keeper->lock), NULL);
    Dprintf("alloc_keeper: keeper allocated at %p\n", keeper);

    return keeper;
}


/* _extract_keeper() - extract a connection from the list and free Py_CObject
 *
 * this function does not call Py_*_ALLOW_THREADS macros
 * this function should be called with a lock on the connection object
 */
inline static connkeeper *
_extract_keeper(connobject *c)
{
    PyObject *conn;
    connkeeper *keeper;
    
    if (!(conn = PyList_GetItem(c->avail_conn, 0))) return NULL;
    Py_INCREF(conn);
    PySequence_DelItem(c->avail_conn, 0);
    keeper = (connkeeper *)PyCObject_AsVoidPtr(conn);
    Dprintf("_extract_pgconn: keeper at %p, conn->refcnt = %d\n",
            keeper, conn->ob_refcnt);
    Py_DECREF(conn);
    return keeper;
}


/* request_pgconn() - request a new physical postgresql connection
 *
 * this function tries to get a connection from the list of available
 * connections. if the list is empty, it creates a new connection and
 * immediately use it. if, after obtaining the connection, the number of
 * available connections is still > connobject->minconn, close and free one of
 * the connections. returns -1 on error (connobject->maxconn reached) and 0 if
 * successful.
 *
 * this function locks the new keeper
 * this function should be called with the connection locked
 * this function enters an ALLOW_THREADS wrapper
 */

static int
request_pgconn(cursobject *self)
{
    connobject *owner_conn;
    connkeeper *keeper;
    int ncursor_conn, navail_conn, nopen_conn;

    /* if we return an error, we want the cursor's postgres connection to
       be set to NULL */
    self->pgconn = NULL;
    self->keeper = NULL;

    /* the owner connection of the current cursor */
    owner_conn = self->conn;
    
    ncursor_conn = PyList_Size(owner_conn->cursors);
    navail_conn = PyList_Size(owner_conn->avail_conn);
    nopen_conn = ncursor_conn + navail_conn;
    
    Dprintf("request_pgconn: maxconn = %d, minconn = %d, openconn = %d, "
            "availconn = %d\n", owner_conn->maxconn, owner_conn->minconn,
            nopen_conn, navail_conn);

    /* if we have at least 1 connection available we use it */
    if (navail_conn > 0) {
        Dprintf("request_pgconn: we have %d available connections\n",
                navail_conn);

        if (!(keeper = _extract_keeper(owner_conn))) {
            return -1;
        }   
        
        /* if there are more connections than the minimum number, we close one
           (we ask for another connection and, if we can get it we close it,
            just to not let the number of connections grow without control) */
        if (--navail_conn > owner_conn->minconn) {
            connkeeper *closing = _extract_keeper(owner_conn);
            if (closing) {
                Dprintf("request_pgconn: destoying keeper at %p\n", keeper);
                PQfinish(closing->pgconn);
                pthread_mutex_destroy(&(closing->lock));
                free(closing);
            }
        }
    }

    /* if we don't have any free connection we allocate one, provided the
       number of total connections does not exceed maxconn */
    else if (nopen_conn  < owner_conn->maxconn) {
        keeper = alloc_keeper(owner_conn);
        if (!keeper) return -1;
    }
    
    else {
        char *errstr = NULL;
        int memerr;

        memerr = asprintf(&errstr, "too many open connections: %i\n"
                          "Try increasing maximum number of physical "
                          "connections when calling connect()", nopen_conn);
        if (memerr < 0) {
            PyErr_SetFromErrno(OperationalError);
        }
        else {
            PyErr_SetString(OperationalError, errstr);
            free(errstr);
        }
        return -1;
    }

    keeper->refcnt = 1;
    self->keeper = keeper;
    self->pgconn = keeper->pgconn;
    
    Dprintf("request_pgconn: cursor at %p using keeper at %p, "
            "connection at %p\n", self, keeper, keeper->pgconn);
    return 0;
}




/* curs_switch_isolation level() - switch isolation level
 *
 * TODO: develop a clean switching policy
 */
void
curs_switch_isolation_level(cursobject *self, long int level)
{
    pthread_mutex_lock(&(self->keeper->lock));
    
    /* if the cursor was not in autocommit and we set autocommit we need to
       ABORT the transaction */
    if (self->isolation_level > 0 && level == 0) {
        if (abort_pgconn(self) < 0) goto error;    
    }

    /* else we simply set the right isolation level (the first call to
       execute will begin the right transaction */
    self->isolation_level = level;
    
 error:
    Dprintf("curs_switch_isolation_level: pgconn = %p, level = %d, "
            "status = %d\n", self->pgconn, self->isolation_level,
            self->keeper->status);
    pthread_mutex_unlock(&(self->keeper->lock));
}


/* CURSOR METHODS */

/* psyco_curs_reset() - reset the cursor
 *
 * this function locks the keeper
 * this function enters an ALLOW_THREADS wrapper
 */
static void
psyco_curs_reset(cursobject *self, int resetconn)
{   
    /* initialize some variables to default values */
    self->notuples = 1;
    self->rowcount = -1;
    self->row = 0;
    
    Py_XDECREF(self->description);
    Py_INCREF(Py_None);
    self->description = Py_None;

    Py_XDECREF(self->status);
    Py_INCREF(Py_None);
    self->status = Py_None;

    Py_XDECREF(self->casts);
    self->casts = NULL;

    if (resetconn) {
        pthread_mutex_lock(&(self->keeper->lock));
        Py_BEGIN_ALLOW_THREADS;
        abort_pgconn(self);
        pthread_mutex_unlock(&(self->keeper->lock));
        Py_END_ALLOW_THREADS;
    }
}


/* psyco_curs_abort() - execute a rollback
 *
 * this function locks the keeper
 * this function enters an ALLOW_THREADS wrapper
 */

static char psyco_curs_abort__doc__[] = "Roll back the cursor.";
static PyObject *
psyco_curs_abort(cursobject *self, PyObject *args) {
    PyObject *result = NULL;

    PARSEARGS(args);
    EXC_IFCLOSED(self);
    EXC_IFNOTONLY(self);
    EXC_IFCRITICAL(self);
    
    psyco_curs_reset(self, 0);
    
    pthread_mutex_lock(&(self->keeper->lock));
    Py_BEGIN_ALLOW_THREADS;
    
    if (abort_pgconn(self) < 0) goto cleanup;

    Py_INCREF(Py_None);
    result = Py_None;

 cleanup:
    pthread_mutex_unlock(&(self->keeper->lock));
    Py_END_ALLOW_THREADS;
    EXC_IFCRITICAL(self);
    return result;
}

/* psyco_curs_commit() - execute a commit
 *
 * this function locks the keeper
 * this function enters an ALLOW_THREADS wrapper
 */
static char psyco_curs_commit__doc__[] = "Commit the cursor.";
static PyObject *
psyco_curs_commit(cursobject *self, PyObject *args) {
    PyObject *result = NULL;

    PARSEARGS(args);
    EXC_IFCLOSED(self);
    EXC_IFNOTONLY(self);
    EXC_IFCRITICAL(self);
    
    psyco_curs_reset(self, 0);
    
    pthread_mutex_lock(&(self->keeper->lock));
    Py_BEGIN_ALLOW_THREADS;
    
    if (commit_pgconn(self) < 0) goto cleanup;
    Py_INCREF(Py_None);
    result = Py_None;

 cleanup:
    pthread_mutex_unlock(&(self->keeper->lock));
    Py_END_ALLOW_THREADS;
    EXC_IFCRITICAL(self);
    return result;
}


/* _psyco_curs_execute() - execute a query and parse results, used by both the
   .execute() and the .callproc() methods */
static PyObject *
_psyco_curs_execute(cursobject *self, char *query,
                    _psyco_curs_execute_callback cb, PyObject *cb_args)
{
    int pgstatus, old_keeper_status;
    PyObject *res = NULL;
    
    /* even if we fail, we remove any information about the previous query */
    psyco_curs_reset(self, 0);

    /* if the status of the connection is critical raise an exception */
    EXC_IFCRITICAL(self);

    assert(self->pgconn);
    if (PQstatus(self->pgconn) != CONNECTION_OK) {
        Dprintf("_psyco_curs_execute: connection NOT OK\n");
        PyErr_SetString(OperationalError, PQerrorMessage(self->pgconn));
        /* the connection is probably dead! bail out NOW! */
        return NULL;
    }
    Dprintf("_psyco_curs_execute: connection at %p OK\n", self->pgconn);

    pthread_mutex_lock(&(self->keeper->lock));
    Py_BEGIN_ALLOW_THREADS;
    Dprintf("_psyco_curs_execute: query = >%s<\n", query);
    begin_pgconn(self);
    IFCLEARPGRES(self->pgres);
    self->pgres = PQexec(self->pgconn, query);
    Dprintf("_psyco_curs_execute: query executed\n");
    pthread_mutex_unlock(&(self->keeper->lock));
    Py_END_ALLOW_THREADS;

    /* check for PGRES_FATAL_ERROR result */
    if (self->pgres == NULL) {
        pgconn_set_critical(self);
        return pgconn_resolve_critical(self);
    }

    /* now we set the status to LOCKED, to avoid unwanted commits or rollbacks
       from other threads. if another thread expect to sync *everything* with
       a single call to .commit() on the connection, it better has to sync
       with us. because us and him are about to work on the same keeper,
       setting status to LOCKED is like shifting *our* execution time a little
       bit in the future... */
    pthread_mutex_lock(&(self->keeper->lock));
    old_keeper_status = self->keeper->status;
    self->keeper->status = KEEPER_LOCKED;
    pthread_mutex_unlock(&(self->keeper->lock));
    
    pgstatus = PQresultStatus(self->pgres);
    
    switch(pgstatus) {

        /* send data to the backend */
    case PGRES_COPY_OUT:
        Dprintf("_psyco_curs_execute: command returned COPY_OUT\n");
        /* fall through to the COPY_IN */

        /* data from the backend */
    case PGRES_COPY_IN:
        Dprintf("_psyco_curs_execute: command returned COPY_IN\n");
        
        /* call the callback */
        if (cb && cb_args) {
            res = cb(self, cb_args);
            if (PyErr_Occurred()) {
                Py_XDECREF(res);
                goto error;
            }
        }
        else {
            PyErr_SetString(ProgrammingError,
                            "COPY TO/COPY FROM can't be used in .execute()");
            goto error;
        }
        break;

        /* tuples, this was a select */
    case PGRES_TUPLES_OK: {
        int i, pgnfields = PQnfields(self->pgres);
        int pgbintuples = PQbinaryTuples(self->pgres);
        int *dsize = NULL;

        self->notuples = 0;
        self->rowcount = PQntuples(self->pgres);
        
        Dprintf("_psyco_curs_execute: got %ld tuples\n", self->rowcount);

        /* create the tuple for description and typecasting */
        Py_XDECREF(self->description); Py_XDECREF(self->casts);
        self->description = PyTuple_New(pgnfields);
        self->casts = PyTuple_New(pgnfields);
        self->columns = pgnfields;

        /* backend status message */
        Py_XDECREF(self->status);
        self->status = PyString_FromString(PQcmdStatus(self->pgres));
        
		/* Calculate the display size for each column */
#ifndef NO_DISPLAY_SIZE
		dsize = (int *)calloc(pgnfields, sizeof(int));
		if (dsize != NULL) {
			if (self->rowcount == 0) {
				for (i=0; i < pgnfields; i++)
					dsize[i] = -1;
			}
			else {
				int j, len;
				for (j = 0; j < self->rowcount; j++) {
					for (i = 0; i < pgnfields; i++) {
                         len = PQgetlength(self->pgres, j, i);
                         if (len > dsize[i]) dsize[i] = len;
                    }
				}
			}
		}
#endif

        for (i = 0; i < pgnfields; i++) {
            /* int j, len = 0, maxl = 0; */
            Oid ftype = PQftype(self->pgres, i);
            int fsize = PQfsize(self->pgres, i);
            int fmod =  PQfmod(self->pgres, i);
            
            PyObject *dtitem = PyTuple_New(7);
            PyObject *type = PyInt_FromLong(ftype);
            PyObject *cast;

            PyTuple_SET_ITEM(self->description, i, dtitem);

            /* fill the right cast function by accessing the global
               dictionary of casting objects.  If we got no defined cast
               use the default one.
            */
            if (!(cast = PyDict_GetItem(psyco_types, type))) {
                Dprintf("_psyco_curs_execute: cast %d not found, using "
                        "default\n", PQftype(self->pgres,i));
                cast = psyco_default_cast;
            }
            /* else if we got binary tuples and if we got a field that
               is binary use the default cast.
            */
            else if (pgbintuples && cast == psyco_binary_cast) {
                Dprintf("_psyco_curs_execute: Binary cursor and "
                        "binary field: %i using default cast\n",
                        PQftype(self->pgres,i));
                    cast = psyco_default_cast;
            }
            Dprintf("_psyco_curs_execute: using cast at %p for type %d\n",
                    cast, PQftype(self->pgres,i));
            Py_INCREF(cast);
            PyTuple_SET_ITEM(self->casts, i, cast);

            /* fill the other fields */
            PyTuple_SET_ITEM(dtitem, 0,
                             PyString_FromString(PQfname(self->pgres, i)));
            PyTuple_SET_ITEM(dtitem, 1, type);

            /* display size is the maximum size of this field
               result tuples. */
            if (dsize && dsize[i] >= 0) {
                PyTuple_SET_ITEM(dtitem, 2, PyInt_FromLong(dsize[i]));
            }
            else {
                Py_INCREF(Py_None);
                PyTuple_SET_ITEM(dtitem, 2, Py_None);
            }

            /* size on the backend */
            if (fmod > 0) fmod = fmod - sizeof(int);
            if (fsize == -1) {
                if (ftype == NUMERICOID) {
                    PyTuple_SET_ITEM(dtitem, 3,
                                     PyInt_FromLong((fmod >> 16) & 0xFFFF));
                }
                else { /* If variable length record, return maximum size */
                    PyTuple_SET_ITEM(dtitem, 3, PyInt_FromLong(fmod));
                }
            }
            else {
                PyTuple_SET_ITEM(dtitem, 3, PyInt_FromLong(fsize));
            }

            if (ftype == NUMERICOID) {
                /* precision */
                PyTuple_SET_ITEM(dtitem, 4,
                                 PyInt_FromLong((fmod >> 16) & 0xFFFF));

                /* scale */
                PyTuple_SET_ITEM(dtitem, 5,
                                 PyInt_FromLong((fmod & 0xFFFF) - 4));
            }

            else {
                /* scale */
                Py_INCREF(Py_None);
                PyTuple_SET_ITEM(dtitem, 4, Py_None);

                /* precision */
                Py_INCREF(Py_None);
                PyTuple_SET_ITEM(dtitem, 5, Py_None);
            }

            /* FIXME: null_ok??? */
            Py_INCREF(Py_None);
            PyTuple_SET_ITEM(dtitem, 6, Py_None);
        }
        
		if (dsize) free(dsize);
    }
		break;

        /* ok but no tuples */
    case PGRES_COMMAND_OK:
        Dprintf("_psyco_curs_execute: command returned OK (no tuples)\n");
        /* sets rowcount to the right number of affected tuples */
        self->rowcount = atol(PQcmdTuples(self->pgres));
        /* try to obtain the oid of an insert */
        self->last_oid = PQoidValue(self->pgres);

        /* backend status message */
        Py_XDECREF(self->status);
        self->status = PyString_FromString(PQcmdStatus(self->pgres));
        
        /* psyco_curs_reset already set the other fields */
        break;
    
        /* error! error! */
    default: {
        char *pgerr = PQresultErrorMessage(self->pgres);
        char *errstr = NULL;

        if (asprintf(&errstr, "%s\n%s", pgerr, query) >= 0) {
#if POSTGRESQL_MAJOR >= 8 || POSTGRESQL_MINOR >= 4
            char *pgstate = PQresultErrorField(self->pgres, PG_DIAG_SQLSTATE);

            /* if pgstate is NULL we are using a new libpq to connect to an
               old backend; need to account for it to avoid cirillic :)
               segmentation faults... */
            if ((pgstate && !strncmp(pgstate, "23", 2))
                || (!pgstate &&
                 (!strncmp(pgerr, "ERROR:  Cannot insert a duplicate key", 37)
                  || !strncmp(pgerr, "ERROR:  ExecAppend: Fail to add null",36)
                  || strstr(pgerr, "referential integrity violation"))))
#else       
            if (!strncmp(pgerr, "ERROR:  Cannot insert a duplicate key", 37)
                || !strncmp(pgerr, "ERROR:  ExecAppend: Fail to add null", 36)
                || strstr(pgerr, "referential integrity violation"))
#endif	
                PyErr_SetString(IntegrityError, errstr);
            else
                PyErr_SetString(ProgrammingError, errstr);
            free(errstr);
        }
        else {
            PyErr_SetString(ProgrammingError, pgerr);
        }
        Dprintf("_psyco_curs_execute: error: pgerr = %s\n", pgerr);

        CLEARPGRES(self->pgres);
        goto error;
    }
        break;
    }

    /* check for result (should be at least None) */
    if (res == NULL) {
        Py_INCREF(Py_None);
        res = Py_None;
    }
    
    /* here we replace status with its default value */
    pthread_mutex_lock(&(self->keeper->lock));
    self->keeper->status = old_keeper_status;
    pthread_mutex_unlock(&(self->keeper->lock));
    return res;
    
  error:
    Dprintf("_psyco_curs_execute: error, NOT resetting connection\n");
    pthread_mutex_lock(&(self->keeper->lock));
    self->keeper->status = old_keeper_status;
    pthread_mutex_unlock(&(self->keeper->lock));
    EXC_IFCRITICAL(self);
    return NULL;    
}

static int
_mogrify(PyObject *var, PyObject *fmt, PyObject **new)
{
    PyObject *key, *value, *n, *item;
    char *d, *c;
    int index = 0, force = 0;

    /* from now on we'll use n and replace its value in *new only at the
       end, just before returning */
    n = *new = NULL;
    c = PyString_AsString(fmt);

    while(*c) {
        /* handle plain percent symbol in format string */
        if (c[0] == '%' && c[1] == '%') {
            c+=2; force = 1;
        }
        
        /* if we find '%(' then this is a dictionary */
        else if (c[0] == '%' && c[1] == '(') {

            /* let's have d point the end of the argument */
            for (d = c + 2; *d && *d != ')'; d++);

            if (*d == ')') {
                key = PyString_FromStringAndSize(c+2, d-c-2);
                value = PyObject_GetItem(var, key);
                /* key has refcnt 1, value the original value + 1 */
                
                /*  if value is NULL this is not a dictionary or the key
                    can't be found:we let python set its own exception */
                if (value == NULL) {
                    Py_DECREF(key); /* destroy key */
                    Py_XDECREF(n);  /* destroy n */
                    return -1;
                }

                Dprintf("_mogrify: value refcnt: %d\n", value->ob_refcnt);
                
                if (n == NULL) {
                    n = PyDict_New();
                }
                
                if ((item = PyObject_GetItem(n, key)) == NULL) {
                    PyObject *t = NULL;

                    PyErr_Clear();
                    
                    if (PyString_Check(value)) {
                        t = new_psyco_quotedstringobject(value);
                        /* t is a new object, refcnt = 1 */
                        PyDict_SetItem(n, key, t);
                        /* both key and t refcnt +1, key is at 2 now */
                    }
                    else if (value == Py_None) {
                        t = PyString_FromString("NULL");
                        PyDict_SetItem(n, key, t);
                        /* t is a new object, refcnt = 1, key is at 2 */
                    }
                    else {
                        PyDict_SetItem(n, key, value);
                        Dprintf("_mogrify: set value refcnt: %d\n",
                                value->ob_refcnt);
                    }

                    Py_XDECREF(t); /* t dies here */
                    /* after the DECREF value has the original refcnt plus 1
                       if it was added to the dictionary directly; good */
                    Py_XDECREF(value); 
                }
                else {
                    /* we have an item with one extra refcnt here, zap! */
                    Py_DECREF(item);
                }
                /* if the value is None we need to substitute the formatting
                   char with 's' */
                if (value == Py_None) {
                    while (*d && !isalpha(*d)) d++;
                    if (*d) *d = 's';
                }
                Py_DECREF(key); /* key has the original refcnt now */
                Dprintf("_mogrify: after value refcnt: %d\n",
                        value->ob_refcnt);
            }
            c = d;

        }
        else if (c[0] == '%' && c[1] != '(') {
            value = PySequence_GetItem(var, index);
            /* value has refcnt inc'ed by 1 here */
            
            /*  if value is NULL this is not a sequence or the index is out
                of bounds: let python set its own exception */
            if (value == NULL) {
                Py_XDECREF(n);
                return -1;
            }

            if (n == NULL) {
                n = PyTuple_New(PyObject_Length(var));
            }
            
            /* let's have d point just after the '%' */
            d = c+1;
            
            if (PyString_Check(value)) {
                PyTuple_SET_ITEM(n, index,
                                 new_psyco_quotedstringobject(value));
                
                Py_DECREF(value);
            }
            else if (value == Py_None) {
                PyTuple_SET_ITEM(n, index, PyString_FromString("NULL"));
                while (*d && !isalpha(*d)) d++;
                if (*d) *d = 's';
                Py_DECREF(value);
            }
            else {
                PyTuple_SET_ITEM(n, index, value);
                /* here we steal value ref, no need to DECREF */
            }

            c = d;
            index += 1;
        }
        else {
            c++;
        }
    }

    if (force && n == NULL)
        n = PyTuple_New(0);
    *new = n;
    
    return 0;
}

/* psyco_curs_execute() - prepare and execute a database query */

static char psyco_curs_execute__doc__[] = 
"Prepare and execute a database operation (query or command.)";

static PyObject *
psyco_curs_execute(cursobject *self, PyObject *args)
{
    PyObject *d = NULL, *cvt = NULL, *operation = NULL, *pystr = NULL, *res;
    char *query = NULL;
    
    if (!PyArg_ParseTuple(args, "O!|O",
                          &PyString_Type, &operation, &d)) {
        return NULL;
    }

    EXC_IFCLOSED(self);
    IFCLEARPGRES(self->pgres);

    Dprintf("psyco_curs_execute: operation = >%s<\n",
            PyString_AsString(operation));

    /* we got a dictionary of values to substitute in the query string, we do
       that by first mogrifying the format string and the dict/tuple, then
       using standard python methods */
    if (d)
    {
        if (_mogrify(d, operation, &cvt) == -1) return NULL;
    }

    if (d && cvt) {
        /* if PyString_Format() return NULL an error occured: if the error
           is a TypeError we need to check the exception.args[0] string for
           the values:
           
               "not enough arguments for format string"
               "not all arguments converted"

           and return the appropriate ProgrammingError. we do that by
           grabbing the curren exception (we will later restore it if the
           type or the strings do not match). */
        if (cvt != NULL && !(pystr = PyString_Format(operation, cvt))) {
            PyObject *err, *arg, *trace;
            int pe = 0;

            Dprintf("psyco_curs_execute: PyString_Format() error\n");
            
            PyErr_Fetch(&err, &arg, &trace);
            
            if (err && PyErr_GivenExceptionMatches(err, PyExc_TypeError)) {
                Dprintf("psyco_curs_execute: TypeError exception catched\n");
                
                PyErr_NormalizeException(&err, &arg, &trace);
                Dprintf("psyco_curs_execute: exception normalized\n");
                
                if (PyObject_HasAttrString(arg, "args")) {
                    PyObject *args = PyObject_GetAttrString(arg, "args");
                    PyObject *str = PySequence_GetItem(args, 0);
                    char *s = PyString_AS_STRING(str);

                    Dprintf("psyco_curs_execute: s = %s\n", s);

                    if (!strcmp(s, "not enough arguments for format string")
                      || !strcmp(s, "not all arguments converted")) {
                        Dprintf("psyco_curs_execute: exception matches\n");
                        PyErr_SetString(ProgrammingError, s);
                        pe = 1;
                        Dprintf("psyco_curs_execute: new exception set\n");
                    }

                    Py_DECREF(args);
                    Py_DECREF(str);
                    Dprintf("psyco_curs_execute: arguments destroyed\n");
                }
            }

            /* if we did not manage our own exception, restore old one */
            if (pe == 1) {
                Py_XDECREF(err); Py_XDECREF(arg); Py_XDECREF(trace);
            }
            else {
                PyErr_Restore(err, arg, trace);
            }
            return NULL;
        }
        query = strdup(PyString_AsString(pystr));

        Dprintf("psyco_curs_execute: cvt->refcnt = %d\n", cvt->ob_refcnt);
        
        Py_DECREF(pystr);
        Py_DECREF(cvt);
    }
    else {
        query = strdup(PyString_AsString(operation));
    }

    Dprintf("psyco_curs_execute: operation->refcnt = %d\n",
            operation->ob_refcnt);

    res = _psyco_curs_execute(self, query, NULL, NULL);
    free(query);
    return res;
}


/* utility funcion used by both callproc() and executemany() */
inline static int
_psyco_curs_tuple_converter(PyObject *o, PyObject **res)
{
    if ((*res = PySequence_Tuple(o)) == NULL) {
        return 0;
    }
    return 1;
}

/* psyco_curs_callproc() - call a stored procedure */

static char psyco_curs_callproc__doc__[] =
"Call a stored database procedure with the given name.";

static PyObject *
psyco_curs_callproc(cursobject *self, PyObject *args)
{
    PyObject *procstring, *parm_seq, *seq;
    char *procname, *query;
    int procnamelen;
    
    if (!PyArg_ParseTuple(args, "O!|O&",
                          &PyString_Type, &procstring,
                          _psyco_curs_tuple_converter, &parm_seq)) {
        return NULL;
    }
    
    EXC_IFCLOSED(self);
    IFCLEARPGRES(self->pgres);

    procname = PyString_AsString(procstring);
    procnamelen = strlen(procname);
    
    /* we got a tuple of values to copy and return */
    if (parm_seq)
    {
        int i, len;
        PyObject *tmp, *fmt;
        char *format;

        len = PyTuple_Size(parm_seq);
        seq = PyTuple_New(len);

        /* setup a format string, we'll fill it in the for loop */
        format = (char *)calloc(1, sizeof(char)*(len*3+9+procnamelen));
        strcpy(format, "SELECT ");
        strcpy(&format[7], procname);
        format[7+procnamelen] = '(';
        
        for (i=0; i<len; i++) {
            tmp = PyTuple_GET_ITEM(parm_seq, i);
            PyTuple_SET_ITEM(seq, i, tmp);
            Py_INCREF(tmp);

            format[i*3+8+procnamelen] = '%';
            format[i*3+9+procnamelen] = 's';
            format[i*3+10+procnamelen] = ',';
        }

        /* finish filling up the format string */
        format[i*3+7+procnamelen] = ')';
        Dprintf("psyco_curs_callproc: format = `%s'\n", format);
            
        /* create the format string in tmp and fill it */
        tmp = PyString_FromString(format);
        if (!(fmt = PyString_Format(tmp, parm_seq))) {
            Py_DECREF(seq);
            Py_DECREF(tmp);
            free(format);
            return NULL;
        }
                
        query = strdup(PyString_AsString(fmt));
        Py_DECREF(tmp);
        Py_DECREF(fmt);
        free(format);
    }
    else {
        PyObject *fmt, *tmp;

        tmp = PyString_FromString("SELECT %s()");
        if (!(fmt = PyString_Format(tmp, procstring))) {
            Py_DECREF(tmp);
            return NULL;
        }
        query = strdup(PyString_AsString(fmt));
        Py_DECREF(tmp);
        Py_DECREF(fmt);
        seq = Py_None;
        Py_INCREF(seq);
    }


    _psyco_curs_execute(self, query, NULL, NULL);
    free(query);
    return seq;
}


/* psyco_curs_executemany() - execute a set of queries */
static char psyco_curs_executemany__doc__[] = 
"Prepare a database operation (query or command) and then execute it "
"against all parameter sequences or mappings found in the sequence "
"seq_of_parameters.";

static PyObject *
psyco_curs_executemany(cursobject *self, PyObject *args)
{
    PyObject *tmpobj, *parm_seq = NULL, *seq_item = NULL;
    PyObject *operation = NULL, *last = NULL;
    int i;

    if (!PyArg_ParseTuple(args, "O!O&",
                          &PyString_Type, &operation,
                          _psyco_curs_tuple_converter, &parm_seq)) {
        return NULL;
    }

    EXC_IFCLOSED(self);
    
    tmpobj = PyTuple_New(2);
    
    Py_INCREF(operation);
    PyTuple_SET_ITEM(tmpobj, 0, operation);
    for (i = 0; i < PyTuple_Size(parm_seq); i++) {
        seq_item = PySequence_GetItem(parm_seq, i); 
        if (!PyDict_Check(seq_item) && !PyTuple_Check(seq_item)) {
            PyErr_SetString(PyExc_TypeError,
                            "arg 2 must be a dictionary or tuple sequence");
	    Py_DECREF(tmpobj);
	    Py_DECREF(seq_item);
	    Py_DECREF(parm_seq);
            return NULL;
        }
        PyTuple_SET_ITEM(tmpobj, 1, seq_item);
	if (last != NULL) {
        Py_DECREF(last);
	}
	last = seq_item;
        if(!psyco_curs_execute(self, tmpobj)) {
            Py_DECREF(tmpobj);
            Py_DECREF(parm_seq);
            return NULL;
        }
    }

    self->rowcount = -1;
    
    Py_DECREF(tmpobj);
    Py_DECREF(parm_seq);
    Py_INCREF(Py_None);
    return Py_None;
}


/* psyco_curs_fetchone() - fetch onw row of data */

static char psyco_curs_fetchone__doc__[] = 
"Fetch the next row of a query result set, returning a "
"single sequence, or None when no more data is available.";

static PyObject *
psyco_curs_fetchone(cursobject *self, PyObject *args)
{
    int coln, i;
    PyObject *res;
    PGresult *r;
    
    PARSEARGS(args);
    EXC_IFCLOSED(self);
    EXC_IFNOTUPLES(self);

    Dprintf("psyco_curs_fetchone: fetching row %ld\n", self->row);
    
    if (self->row >= self->rowcount) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    
    coln = PQnfields(self->pgres);
    res = PyTuple_New(coln);

    r = self->pgres;
    for (i = 0; i < coln; i++) {
        PyObject *val, *str;
        PyObject *arg = PyTuple_New(1);
        
        if (PQgetisnull(r, self->row, i)) {
            Py_INCREF(Py_None);
            str = Py_None;
            Dprintf("psyco_curs_fetchone: row %ld, element %d is None\n",
                    self->row, i);
        }
        else {
            char *s = PQgetvalue(r, self->row, i);
            int l = PQgetlength(r, self->row, i);
            str = PyString_FromStringAndSize(s, l);
            Dprintf("psyco_curs_fetchone: row %ld, element %d, len %i\n",
                    self->row, i, l);
        }

        Dprintf("psyco_curs_fetchone: str->refcnt = %d\n", str->ob_refcnt);
        PyTuple_SET_ITEM(arg, 0, str);
        val = PyObject_CallObject(PyTuple_GET_ITEM(self->casts, i), arg);    
        if (val) {
            Dprintf("psyco_curs_fetchone: val->refcnt = %d\n", val->ob_refcnt);
        }
            Dprintf("psyco_curs_fetchone: str->refcnt = %d\n", str->ob_refcnt);
        Py_DECREF(arg);
        
        if (val) {
            PyTuple_SET_ITEM(res, i, val);
        }
        else {
            /* an error occurred in the type system, we return NULL to raise
               an exception. the typecast code should already have set the
               exception type and text */
            res = NULL;
            break;
        }
    }
    
    self->row++; /* move the counter to next line */
    return res;
}


/* psyco_curs_fetchmany() - fetch many rows */
   
static char psyco_curs_fetchmany__doc__[] = 
"Fetch the next set of rows of a query result, returning a sequence of "
"sequences (e.g. a list of tuples). An empty sequence is returned when "
"no more rows are available.";

static PyObject *
psyco_curs_fetchmany(cursobject *self, PyObject *args, PyObject *kwords)
{
    long int size, i;
    PyObject *list, *res;
    static char *kwlist[] = {"size", NULL};
    
    size = self->arraysize;
    if (!PyArg_ParseTupleAndKeywords(args, kwords, "|l", kwlist, &size)) {
        return NULL;
    }

    EXC_IFCLOSED(self);
    EXC_IFNOTUPLES(self);

    /* make sure size is not > than the available number of rows */
    if (size > self->rowcount - self->row || size < 0) {
        size = self->rowcount - self->row;
    }

    /* size < 0 should never happen with the calculation above */
    assert(size >= 0); 
    
#ifdef DBAPIEXTENSIONS
    /* if size is <= 0 there are no more rows, we return an error */
    if (size == 0) {
        PyErr_SetString(ProgrammingError, "no more results");
        return NULL;
    }
#endif
    
    list = PyList_New(size);
    for (i = 0; i < size; i++) {
        res = psyco_curs_fetchone(self, NULL);

        if (res == NULL) {
            Py_DECREF(list);
            return NULL;
        }

        /* the result can't be None, because we *know* how many rows to fetch */
        assert(res != Py_None);
        
        PyList_SET_ITEM(list, i, res);
    }
    return list;
}


/* psyco_curs_fetchall() - fetch all remaining rows */

static char psyco_curs_fetchall__doc__[] = 
"Fetch all (remaining) rows of a query result, returning "
"them as a sequence of sequences (e.g. a list of tuples).";

static PyObject *
psyco_curs_fetchall(cursobject *self, PyObject *args)
{
    PyObject *res, *list;
    int size, i;

    PARSEARGS(args);
    EXC_IFCLOSED(self);
    EXC_IFNOTUPLES(self);
    
    size = self->rowcount - self->row;
    list = PyList_New(size);
    
#ifdef DBAPIEXTENSIONS
    /* if size is <= 0 there are no more rows, we return an error */
    if (size == 0) {
        PyErr_SetString(ProgrammingError, "no more results");
        return NULL;
    }
#endif
    
    for (i = 0; i < size; i++) {
        res = psyco_curs_fetchone(self, NULL);
        
        if (res == NULL){
            Py_DECREF(list);
            return NULL;
        }
        assert(res != Py_None);
        PyList_SET_ITEM(list, i, res);
    }
    return list;
}


/* psyco_curs_dictfetchone() - fetch one row of data in a dictionary */

static char psyco_curs_dictfetchone__doc__[] = 
"Fetch the next row of a query result set, returning a "
"single dictionary, or None when no more data is available.";

static PyObject *
psyco_curs_dictfetchone(cursobject *self, PyObject *args)
{
    int i;
    PyObject *dict, *res, *desc, *name;
    PyObject *value = NULL;
    
    res = psyco_curs_fetchone(self, args);
    if (res == NULL || res == Py_None) return res;

    dict = PyDict_New();
    for (i=0; i<self->columns; i++) {
        /* obtain name from description (we incref it because the python
           interpreter will decref on dict death) then set column value using
           name as mapping key */
        desc = PyTuple_GET_ITEM(self->description, i);
        name = PyTuple_GET_ITEM(desc, 0);
        value = PyTuple_GET_ITEM(res, i);
        Dprintf("psyco_curs_dictfetchone: name.ob_refcnt = %d, "
                "value.ob_refcnt = %d\n", name->ob_refcnt, value->ob_refcnt);
        PyDict_SetItem(dict, name, value);
        Dprintf("psyco_curs_dictfetchone: name.ob_refcnt = %d, "
                "value.ob_refcnt = %d\n", name->ob_refcnt, value->ob_refcnt);
    }
    
    Dprintf("psyco_curs_dictfetchone: res.ob_refcnt = %d\n", res->ob_refcnt);
    Py_DECREF(res);
    return dict;
}

/* psyco_curs_dictfetchmany() - fetch many rows into dictionaries */
   
static char psyco_curs_dictfetchmany__doc__[] = 
"Fetch the next set of rows of a query result, returning a sequence of "
"dictionaries. An empty sequence is returned when "
"no more rows are available.";

static PyObject *
psyco_curs_dictfetchmany(cursobject *self, PyObject *args, PyObject *kwords)
{
    long int size, i;
    PyObject *list, *res;
    static char *kwlist[] = {"size", NULL};
    
    size = self->arraysize;
    if (!PyArg_ParseTupleAndKeywords(args, kwords, "|l", kwlist, &size)) {
        return NULL;
    }

    EXC_IFCLOSED(self);
    EXC_IFNOTUPLES(self);

    /* make sure size is not > than the available number of rows */
    if (size > self->rowcount - self->row || size < 0) {
        size = self->rowcount - self->row;
    }

    /* size < 0 should never happen with the calculation above */
    assert(size >= 0); 
    
#ifdef DBAPIEXTENSIONS
    /* if size is <= 0 there are no more rows, we return an error */
    if (size == 0) {
        PyErr_SetString(ProgrammingError, "no more results");
        return NULL;
    }
#endif
    
    list = PyList_New(size);
    for (i = 0; i < size; i++) {
        res = psyco_curs_dictfetchone(self, NULL);

        if (res == NULL) {
            Py_DECREF(list);
            return NULL;
        }

        /* the result can't be None, because we *know* how many rows to
           fetch */
        assert(res != Py_None);
        
        PyList_SET_ITEM(list, i, res);
    }
    return list;
}


/* psyco_curs_dictfetchall() - fetch all remaining rows in dictionaries */

static char psyco_curs_dictfetchall__doc__[] = 
"Fetch all (remaining) rows of a query result, returning "
"them as a sequence of dictionaries.";

static PyObject *
psyco_curs_dictfetchall(cursobject *self, PyObject *args)
{
    PyObject *res, *list;
    int size, i;

    EXC_IFCLOSED(self);
    EXC_IFNOTUPLES(self);
    
    size = self->rowcount - self->row;
    list = PyList_New(size);
    
#ifdef DBAPIEXTENSIONS
    /* if size is <= 0 there are no more rows, we return an error */
    if (size == 0) {
        PyErr_SetString(ProgrammingError, "no more results");
        return NULL;
    }
#endif
    
    for (i = 0; i < size; i++) {
        res = psyco_curs_dictfetchone(self, NULL);
        
        if (res == NULL){
            Py_DECREF(list);
            return NULL;
        }
        assert(res != Py_None);
        PyList_SET_ITEM(list, i, res);
    }
    return list;
}


/* TODO: psyco_curs_setinputsizes() - set the input size */

static char psyco_curs_setinputsizes__doc__[] =
"Set a column buffer size for executeXXX() operation's parameters.\n"
"This can be used before a call to executeXXX() to predefine memory areas\n"
"for the operation's parameters. Sizes is specified as a sequence -- one \n"
"item for each input parameter.\n"
"At now this method does nothing.";

static PyObject *
psyco_curs_setinputsizes(cursobject *self, PyObject *args)
{
    EXC_IFCLOSED(self);
    Py_INCREF(Py_None);
    return Py_None;
}


/* TODO: psyco_curs_setoutputsizes() - set the output size */

static char psyco_curs_setoutputsizes__doc__[] = 
"Set a column buffer size for fetches of large columns.\n"
"The column is specified as an index into the result sequence. Not\n"
"specifying the column will set the default size for all large columns\n"
"in the cursor.\n"
"At now this method does nothing.";

static PyObject *
psyco_curs_setoutputsize(cursobject *self, PyObject *args)
{
    long int size, column;

    if (!PyArg_ParseTuple(args, "l|l", &size, &column))
        return NULL;

    EXC_IFCLOSED(self);
    
    Py_INCREF(Py_None);
    return Py_None;
}


/* TODO: psyco_curs_nextset() - skip to next set of results */

static char psyco_curs_nextset__doc__[] = 
"Skip to the next set.\n"
"This method will make the cursor skip to the next available set, discarding\n"
"any remaining rows from the current set (at now this method does nothing,\n"
"because PostgreSQL does not support multiple sets.";

static PyObject *
psyco_curs_nextset(cursobject *self, PyObject *args)
{
    PARSEARGS(args);
    EXC_IFCLOSED(self);
    self->row = self->rowcount;
    Py_INCREF(Py_None);
    return Py_None;
}


/* psyco_curs_autocommit() - sets autocommit on the cursor */

static char psyco_curs_autocommit__doc__[] =
"Sets the cursor to autocommit mode.""";

static PyObject *
psyco_curs_autocommit(cursobject *self, PyObject *args)
{
    long int ac = 1; /* the default is to set autocommit on */
    
    if (!PyArg_ParseTuple(args, "|l", &ac)) {
        return NULL;
    }

    Dprintf("psyco_curs_autocommit: autocommit = %ld on %p\n", ac, self);

    if (ac) ac = 0;
    else ac = 2;

    EXC_IFNOTONLY(self);
    EXC_IFCRITICAL(self);
    curs_switch_isolation_level(self, ac);
    EXC_IFCRITICAL(self);

    Py_INCREF(Py_None);
    return Py_None;
}


/* psyco_curs_lastoid() - return the last oid from an insert or None */

static char psyco_curs_lastoid__doc__[] =
"Return the last oid from an insert or None.""";

static PyObject *
psyco_curs_lastoid(cursobject *self, PyObject *args)
{    
    PARSEARGS(args);
    EXC_IFCRITICAL(self);
    
    Dprintf("psyco_curs_lastoid: self->last_oid = %d on %p\n",
            self->last_oid, self);

    if (self->last_oid != InvalidOid) {
        return PyInt_FromLong((long)self->last_oid);
    }
    else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}


/* psyco_curs_copy_to() - copy a table to a local file */

static char psyco_curs_copy_to__doc__[] =
"Copy table to a local file.\ncopy_to(fileObject, tablename, <separator>, <null identifier>)""";

static PyObject *
_psyco_curs_copy_to(cursobject *self, PyObject *file)
{
    char buffer[4096];
    int status, len;
    PyObject *o;
    
    while (1) {
        status = PQgetline(self->pgconn, buffer, 4096);
        if (status == 0) {
            if (buffer[0] == '\\' && buffer[1] == '.') break;
            
            len = strlen(buffer);
            buffer[len++] = '\n';
        }
        else if (status == 1) {
            len = 4096-1;
        }
        else {
            return NULL;
        }

        o = PyString_FromStringAndSize(buffer, len);
        PyObject_CallMethod(file, "write", "O", o);
        Py_DECREF(o);
    }

    if (PQendcopy(self->pgconn) != 0) return NULL;
    
    Py_INCREF(Py_None);
    return Py_None;
}
     
static PyObject *
psyco_curs_copy_to(cursobject *self, PyObject *args)
{
    char *table_name, *query = NULL;
    char *sep = "\t", *null =NULL;
    PyObject *file;

    if (!PyArg_ParseTuple(args, "Os|ss", &file, &table_name, &sep, &null)) {
        return NULL;
    }
    if (!PyObject_GetAttrString(file, "write")) {
        return NULL;
    }
    
    EXC_IFCRITICAL(self);

    if (null) {
        asprintf(&query, "COPY %s TO stdout USING DELIMITERS '%s'"
                     " WITH NULL AS '%s'", table_name, sep, null);
    }
    else {
        asprintf(&query, "COPY %s TO stdout USING DELIMITERS '%s'" ,
                 table_name, sep);
    }
    Dprintf("psyco_curs_copy_to: query = %s\n", query);
    
    _psyco_curs_execute(self, query, _psyco_curs_copy_to, file);
    free(query);

    Py_INCREF(Py_None);
    return Py_None;
}

static char psyco_curs_scroll__doc__[] =
"Scroll the cursor in the result set to a new position according to mode.\n"
"scroll(value[,mode='relative'])";

static PyObject *
psyco_curs_scroll(cursobject *self, PyObject *args, PyObject *kwords)
{
    int value, newpos;
    char *mode = "relative";

    static char *kwlist[] = {"value", "mode", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, kwords, "i|s",
                                     kwlist, &value, &mode)) {
        return NULL;
    }

    if (strcmp(mode, "relative") == 0) {
        newpos = self->row + value;
    } else if ( strcmp( mode, "absolute") == 0) {
        newpos = value;
    } else {
        PyErr_SetString(ProgrammingError,
                        "scroll mode must be 'relative' or 'absolute'");
        return NULL;
    }

    if (newpos < 0 || newpos >= self->rowcount ) {
        PyErr_SetString(PyExc_IndexError,
                        "scroll destination is out of bounds");
        return NULL;
    }
    self->row = newpos;

    Py_INCREF(Py_None);
    return Py_None;
}

/* psyco_curs_copy_from() - copy a table from a local file */

static char psyco_curs_copy_from__doc__[] =
"Copy table from a local file.\ncopy_from(fileObject, tablename, <separator>, <null identifier>)""";

static PyObject *
_psyco_curs_copy_from(cursobject *self, PyObject *file)
{
    PyObject *o;
  	
    while (1) {
        o = PyObject_CallMethod(file, "readline", NULL);
        if (!o || o == Py_None || PyString_GET_SIZE(o) == 0) break;
        if (PQputline(self->pgconn, PyString_AS_STRING(o)) != 0) {
            Py_DECREF(o);
            return NULL;
        }
        Py_DECREF(o);
    }
    Py_XDECREF(o);
    PQputline(self->pgconn, "\\.\n");
    PQendcopy(self->pgconn);
   
    Py_INCREF(Py_None);
    return Py_None;
}
     
static PyObject *
psyco_curs_copy_from(cursobject *self, PyObject *args)
{
    char *table_name, *query = NULL;
    char *sep = "\t", *null = NULL;
    PyObject *file, *res;

    if (!PyArg_ParseTuple(args, "Os|ss", &file, &table_name, &sep, &null)) {
        return NULL;
    }
    if (!PyObject_HasAttrString(file, "readline")) {
        return NULL;
    }

    EXC_IFCRITICAL(self);

    if (null) {
        asprintf(&query, "COPY %s FROM stdin USING DELIMITERS '%s'"
                     " WITH NULL AS '%s'", table_name, sep, null);
    }
    else {
        asprintf(&query, "COPY %s FROM stdin USING DELIMITERS '%s'" ,
                 table_name, sep);
    }
    Dprintf("psyco_curs_copy_from: query = %s\n", query);
    
    res = _psyco_curs_execute(self, query, _psyco_curs_copy_from, file);
    free(query);
    
    return res;
}

static char psyco_curs_notifies__doc__[] = "Returns list of notifies.";

static PyObject *
psyco_curs_notifies(cursobject *self, PyObject *args)
{
    PGnotify *notify;
    PyObject *list, *tuple;

    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    list = PyList_New(0);

    while ((notify = PQnotifies(self->pgconn)) != NULL) {
        tuple=PyTuple_New(2);
        PyTuple_SET_ITEM(tuple,0,PyString_FromString(notify->relname));
        PyTuple_SET_ITEM(tuple,1,PyInt_FromLong(notify->be_pid));
        PyList_Append(list,tuple);
	    PQfreeNotify(notify);
    };

    return list;
}

static char psyco_curs_fileno__doc__[] = "Returns fd of socket associated with cursor.";

static PyObject *
psyco_curs_fileno(cursobject *self, PyObject *args)
{
    return PyInt_FromLong(PQsocket(self->pgconn));
}
/**** CURSOR OBJECT DEFINITION ****/

/* object methods list */

static struct PyMethodDef psyco_curs_methods[] = {
    {"rollback", (PyCFunction)psyco_curs_abort,
     METH_VARARGS, psyco_curs_abort__doc__},    
    {"commit", (PyCFunction)psyco_curs_commit,
     METH_VARARGS, psyco_curs_commit__doc__},
    {"close", (PyCFunction)psyco_curs_close,
     METH_VARARGS, psyco_curs_close__doc__},
    {"callproc", (PyCFunction)psyco_curs_callproc,
     METH_VARARGS, psyco_curs_callproc__doc__},
    {"execute", (PyCFunction)psyco_curs_execute,
     METH_VARARGS, psyco_curs_execute__doc__},
    {"executemany", (PyCFunction)psyco_curs_executemany,
     METH_VARARGS, psyco_curs_executemany__doc__},
    {"fetchone", (PyCFunction)psyco_curs_fetchone,
     METH_VARARGS, psyco_curs_fetchone__doc__},
    {"fetchmany", (PyCFunction)psyco_curs_fetchmany,
     METH_VARARGS|METH_KEYWORDS, psyco_curs_fetchmany__doc__},
    {"fetchall", (PyCFunction)psyco_curs_fetchall,
     METH_VARARGS, psyco_curs_fetchall__doc__},
    {"dictfetchone", (PyCFunction)psyco_curs_dictfetchone,
     METH_VARARGS, psyco_curs_dictfetchone__doc__},
    {"dictfetchmany", (PyCFunction)psyco_curs_dictfetchmany,
     METH_VARARGS|METH_KEYWORDS, psyco_curs_dictfetchmany__doc__},
    {"dictfetchall", (PyCFunction)psyco_curs_dictfetchall,
     METH_VARARGS, psyco_curs_dictfetchall__doc__},
    {"setinputsizes", (PyCFunction)psyco_curs_setinputsizes,
     METH_VARARGS, psyco_curs_setinputsizes__doc__},
    {"setoutputsize", (PyCFunction)psyco_curs_setoutputsize,
     METH_VARARGS, psyco_curs_setoutputsizes__doc__},
    {"nextset", (PyCFunction)psyco_curs_nextset,
     METH_VARARGS, psyco_curs_nextset__doc__},
    {"autocommit", (PyCFunction)psyco_curs_autocommit,
     METH_VARARGS, psyco_curs_autocommit__doc__},
    {"lastoid", (PyCFunction)psyco_curs_lastoid,
     METH_VARARGS, psyco_curs_lastoid__doc__},
    {"copy_to", (PyCFunction)psyco_curs_copy_to,
     METH_VARARGS, psyco_curs_copy_to__doc__},
    {"copy_from", (PyCFunction)psyco_curs_copy_from,
     METH_VARARGS, psyco_curs_copy_from__doc__},
    {"scroll", (PyCFunction)psyco_curs_scroll,
     METH_VARARGS|METH_KEYWORDS, psyco_curs_scroll__doc__},
    {"notifies", (PyCFunction)psyco_curs_notifies,
     METH_VARARGS, psyco_curs_notifies__doc__},
    {"fileno", (PyCFunction)psyco_curs_fileno,
     METH_VARARGS, psyco_curs_fileno__doc__},
    {NULL, NULL}
};


/* object member list */

#define OFFSETOF(x) offsetof(cursobject, x)

static struct memberlist psyco_curs_memberlist[] = {
    {"rowcount", T_LONG, OFFSETOF(rowcount), RO},
    {"arraysize", T_LONG, OFFSETOF(arraysize), 0},
    {"description", T_OBJECT,OFFSETOF(description), RO},
    {"lastrowid", T_LONG,OFFSETOF(last_oid), RO},
    {"statusmessage", T_OBJECT,OFFSETOF(status), RO},
    {NULL}	
};


/* the python object interface for the cursor object */

static PyObject *
psyco_curs_getattr(cursobject *self, char *name)
{
    PyObject *rv;
	
    rv = PyMember_Get((char *)self, psyco_curs_memberlist, name);
    if (rv) return rv;
    PyErr_Clear();
    return Py_FindMethod(psyco_curs_methods, (PyObject *)self, name);
}

static int
psyco_curs_setattr(cursobject *self, char *name, PyObject *v)
{
    if (v == NULL) {
        PyErr_SetString(PyExc_AttributeError, "cannot delete attribute");
        return -1;
    }
    return PyMember_Set((char *)self, psyco_curs_memberlist, name, v);
}


static void
psyco_curs_destroy(cursobject *self)
{   
    Dprintf("psyco_curs_destroy: destroying cursor at %p, refcnt = %d\n",
            self, self->ob_refcnt);
    
    /* destroy cursor information */
    _psyco_curs_destroy(self);
    Py_XDECREF(self->description);
    Py_XDECREF(self->status);

    /* close unwanted open connections to keep the number of opened
       connections low */
    if (self->conn
        && PyList_Size(self->conn->avail_conn) > self->conn->minconn) {
        connkeeper *closing = _extract_keeper(self->conn);
        if (closing) {
            PQfinish(closing->pgconn);
            pthread_mutex_destroy(&(closing->lock));
            free(closing);
        }
    }
    
    Dprintf("psyco_curs_destroy: cursor at %p destroyed, refcnt = %d\n",
            self, self->ob_refcnt);
    PyObject_Del(self);
}


static char Cusrtype__doc__[] = 
"Cursor Object.  These objects represent a database cursor, which is "
"used to manage the context of a fetch operation.";

static PyTypeObject Curstype = {
#if !defined(_WIN32) && !defined(__CYGWIN__)
    PyObject_HEAD_INIT(&PyType_Type)
#else
    PyObject_HEAD_INIT(NULL)
#endif
    0,				       /*ob_size*/
    "cursor",              /*tp_name*/
    sizeof(cursobject),    /*tp_basicsize*/
    0,				       /*tp_itemsize*/
    /* methods */
    (destructor)psyco_curs_destroy,   /*tp_dealloc*/
    (printfunc)0,		             /*tp_print*/
    (getattrfunc)psyco_curs_getattr,	 /*tp_getattr*/
    (setattrfunc)psyco_curs_setattr,	 /*tp_setattr*/
    (cmpfunc)0,		                 /*tp_compare*/
    (reprfunc)0,		             /*tp_repr*/
    0,			                     /*tp_as_number*/
    0,		                         /*tp_as_sequence*/
    0,		                         /*tp_as_mapping*/
    (hashfunc)0,		/*tp_hash*/
    (ternaryfunc)0,		/*tp_call*/
    (reprfunc)0,		/*tp_str*/
  
    /* Space for future expansion */
    0L,0L,0L,0L,
    Cusrtype__doc__ /* Documentation string */
};



/* the C constructor for cursor objects
 *
 * this function locks the connection to access the cursor list
 * if the keeper argument is given, this function should be called
 *   with a lock on the keeper (to be sure that the keeper does not
 *   get deallocated while we are accessing it)
 */
cursobject *
new_psyco_cursobject(connobject *conn, connkeeper *keeper)
{
    cursobject *self;

#if defined(_WIN32) || defined(__CYGWIN__)
    /* For MSVC workaround */
    Curstype.ob_type = &PyType_Type;
#endif
    
    Dprintf("new_psyco_cursobject: new cursor, keeper = %p\n", keeper);

    self = PyObject_NEW(cursobject, &Curstype);
    if (self == NULL) return NULL;

    /* initialize the attributes */
    self->conn = conn;     /* the owning connection */
    self->pgres = NULL;    /* no result yet */
    self->arraysize = 1;
    self->rowcount = -1;
    self->closed = 0;
    self->last_oid = InvalidOid;
    self->isolation_level = conn->isolation_level;
    self->casts = NULL;
    self->notice = NULL;
    self->critical = NULL;
    self->description = Py_None;
    Py_INCREF(Py_None);
    self->status = Py_None;
    Py_INCREF(Py_None);
    
    /* get a connection to db */
    if (keeper != NULL) {   
        self->keeper = keeper;
        self->pgconn = keeper->pgconn;
    }
    else {
        if (request_pgconn(self) == -1) {
            Py_DECREF(self);
            return NULL;
        }
    }

    /* before appending to the list of cursors, we try to lock the connection
       because we don't want to modify the list while another thread is doing
       a full commit or rollback on the connection */
    pthread_mutex_lock(&(conn->lock));
    if(PyList_Append(conn->cursors, (PyObject *)self)) {
        Dprintf("new_psyco_cursobject: PyList_Append() failed\n");
        Py_DECREF(self);
        pthread_mutex_unlock(&(conn->lock));
        return NULL;
    }
    pthread_mutex_unlock(&(conn->lock));

    Py_DECREF(self); /* cursor will remove itself from the list */
    
    Dprintf("new_psyco_cursobject: cursor created at %p, refcnt = %d\n",
            self, self->ob_refcnt);
    return self;
}
