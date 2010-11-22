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
 * module.h -- defines constants needed by the module interface DBAPI 2.0
 * $Id: module.h 554 2004-10-30 00:19:27Z fog $
*/

#ifndef __PSYCOPG_MODULE__
#define __PSYCOPG_MODULE__

#include <Python.h>
#include <structmember.h>
#include <mxDateTime.h>

#include <stdio.h>
#include <libpq-fe.h>
#include <time.h>
#ifndef _WIN32
#include <pthread.h>
#else
#include <windows.h>
#define pthread_mutex_t HANDLE
#define pthread_condvar_t HANDLE
#define pthread_mutex_lock(object) WaitForSingleObject(object, INFINITE)
#define pthread_mutex_unlock(object) ReleaseMutex(object)
#define pthread_mutex_destroy(ref) (CloseHandle(ref))

static int pthread_mutex_init(pthread_mutex_t *mutex, void* temp)
{
  *mutex=CreateMutex(NULL, FALSE, NULL);
  return 0;
}
#define inline

#endif

#include "typeobj.h"


/**** python 1.5 does not have PyObject_Del ****/
#ifndef PyObject_Del
#define PyObject_Del PyMem_DEL
#endif

/**** PyObject_TypeCheck introduced in 2.2 ****/
#ifndef PyObject_TypeCheck
#define PyObject_TypeCheck(o, t) ((o)->ob_type == (t))
#endif

/**** globals of the module ****/
#define APILEVEL "2.0"
#define THREADSAFETY 2
#define PARAMSTYLE "pyformat"


/**** constants of the module ****/
#define MAXCONN 64
#define MINCONN 8
#define MACRO_STR(MACRO) "\"MACRO\""


/**** status of the cursor's keeper ****/
#define KEEPER_READY 0
#define KEEPER_BEGIN 1
#define KEEPER_CONN_LOCK 2
#define KEEPER_CONN_READY 3
#define KEEPER_CONN_BEGIN 4
#define KEEPER_LOCKED -1


/**** debug printf-like function ****/
#if defined( __GNUC__) && !defined(__APPLE__)
#ifdef PSYCOTIC_DEBUG
#include <sys/types.h>
#include <unistd.h>
#define Dprintf(fmt, args...) fprintf(stderr, "[%d] " fmt, getpid() , ## args)
#else
#define Dprintf(fmt, args...)
#endif 
#else /* !__GNUC__ or __APPLE__ */
#ifdef PSYCOTIC_DEBUG
#include <stdarg.h>
static void Dprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
#else
static void Dprintf(const char *fmt, ...) {}
#endif
#endif


/**** global exceptions ****/
PyObject *Error, *Warning, *InterfaceError, 
    *DatabaseError, *InternalError, *OperationalError, *ProgrammingError,
    *IntegrityError, *DataError, *NotSupportedError;


/**** the connection keeper object, used by cursors and connections to
      access PGconnection objects ****/

typedef struct {
    PGconn          *pgconn;
    pthread_mutex_t  lock;
    int              refcnt;
    int              status;
} connkeeper;


/**** the connection object ****/

/* forward declaration of the cursor object */
typedef struct _cursobject cursobject;

typedef struct {
    PyObject_HEAD
    PyObject *cursors;    /* a sequence of cursors on this connection */
    PyObject *avail_conn; /* a sequence of avaliable db connections */
    pthread_mutex_t lock; /* the global connection lock */
    cursobject *stdmanager; /* manager cursor */
    char *dsn;
    int closed;           /* 1 if the connection is closed */
    int maxconn;          /* max connections to db */
    int minconn;          /* minimum number of open connections */
    int isolation_level;  /* isolation level */
    int serialize;        /* serialize cursors? */
} connobject;

connobject *new_psyco_connobject(char *dsn, int maxconn, int minconn,
                                 int serialize);


/**** the cursor object ****/
struct _cursobject {
    PyObject_HEAD
    int closed;              /* 1 if the cursor is closed, -1 need closing */
    int notuples;            /* 1 if the command was not a query */
    connobject *conn;        /* connection owning the cursor */
    PyObject *description;   /* read-only attribute is a sequence of 7-item
			                    sequences.*/

    /* read-only attribute specifies the number of rows that the last
	   executeXXX() produced (for DQL statements like select) or affected (for
	   DML statements like update or insert).*/
    long int rowcount; 

    /* read/write attribute specifies the number of rows to fetch at a time
	   with fetchmany().  It defaults to 1 meaning to fetch a single row at a
	   time.*/
    long int arraysize;
    
    /* the row counter for fetch*() */
    long int row;

    /* number of columns fetched from the db */
    long int columns;

    /* postgres stuff */
    connkeeper *keeper;   /* the connection keeper */
    PGconn     *pgconn;   /* this is a copy of the PGconn in the keeper */
    PGresult   *pgres;

    /* an array (tuple) of typecast functions */
    PyObject *casts;

    /* last message from the server after an execute */
    PyObject *status;
    
    /* last oid from an insert or InvalidOid */
    Oid last_oid;

    /* isolation level for this cursor */
    int isolation_level;

    /* a notice from the backend */
    char *notice;

    /* a critical error, remember to cleanup */
    char *critical;
};

cursobject *new_psyco_cursobject(connobject *conn, connkeeper *keeper);

/**** function type used in execute callbacks ****/
typedef PyObject *(*_psyco_curs_execute_callback)(cursobject *s, PyObject *o);


/**** mxDateTime API access ****/
extern mxDateTimeModule_APIObject *mxDateTimeP;


/**** transaction control functions ****/
extern int dispose_pgconn(cursobject *self);
extern int commit_pgconn(cursobject *self);
extern int abort_pgconn(cursobject *self);
extern int begin_pgconn(cursobject *self);

/**** other globally visible functions ****/
extern void curs_switch_isolation_level(cursobject *self, long int level);
extern connkeeper *alloc_keeper(connobject *conn);


/**** some usefull macros ****/

/* for methods that must return NULL while the object is closed */
#define EXC_IFCLOSED(self) if ((self)->closed) { \
                               PyErr_SetString(InterfaceError,"already closed"); \
                               return NULL; }

/* for methods that must return NULL when last query has no output */
#define EXC_IFNOTUPLES(self) if ((self)->notuples) { \
                                 PyErr_SetString(Error,"no results to fetch"); \
                                 return NULL; }

/* for methods that extend the dbapi and should raise an exception if not
   the only owners of the connkeeper */
#define EXC_IFNOTONLY(self) if ((self)->keeper->refcnt != 1) { \
  PyErr_SetString(Error,"serialized connection: cannot commit on this cursor"); \
                                 return NULL; }

/* check for critical errors */
#define EXC_IFCRITICAL(self) if ((self)->critical) \
                                  return pgconn_resolve_critical(self)

/* methods that need to parse arguments but don't use them */
#define PARSEARGS(args)  if (args && !PyArg_ParseTuple(args, "")) return NULL

/* macro to clean the pg result */
#define IFCLEARPGRES(pgres)  if (pgres) { PQclear(pgres); pgres = NULL; }
#define CLEARPGRES(pgres)    PQclear(pgres); pgres = NULL

#endif /* __PSYCOPG_MODULE__ */
