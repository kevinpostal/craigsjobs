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
 * module.h -- header file for management of type objects
 * $Id: typeobj.h 554 2004-10-30 00:19:27Z fog $
 */

#ifndef __PSYCOPG_TYPEOBJ__
#define __PSYCOPG_TYPEOBJ__


/**** GLOBAL DATA ****/

/* the type dictionary, much faster to access it globally */
extern PyObject *psyco_types;

/* The default casting object, used when no other objects are available */
extern PyObject *psyco_default_cast;
extern PyObject *psyco_binary_cast;


/**** the DBAPI type object ****/

/* type of type-casting functions (both C and Python) */
typedef PyObject *(*dbapitypeobject_cast_function)(PyObject *);

typedef struct {
    PyObject_HEAD

    PyObject *name;    /* the name of this type */
    PyObject *values;  /* the different types this instance can match */

    dbapitypeobject_cast_function  ccast;  /* the C casting function */
    PyObject                      *pcast;  /* the python casting function */
} psyco_DBAPITypeObject;

/* the object type */
extern PyTypeObject psyco_DBAPITypeObject_Type;


/**** the initialization values are stored here ****/

typedef struct {
    char *name;
    int  *values;
    dbapitypeobject_cast_function cast;
}  psyco_DBAPIInitList;


/**** externally visible functions ****/

/* used by module.c to init the type system and register types */
extern int psyco_init_types(PyObject *md);
extern int psyco_add_type(PyObject *obj);

/* the C callable DBAPITypeObject creator function */
PyObject *new_psyco_typeobject(psyco_DBAPIInitList *type);

/* the python callable DBAPITypeObject creator function */
extern PyObject *
psyco_DBAPITypeObject_init(PyObject *self, PyObject *args, PyObject *keywds);

#endif /* __PSYCOPG_TYPEOBJ__ */
