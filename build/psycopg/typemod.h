/*
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
 * typemod.h -- types that reside in the psycopg module
 * $Id: typemod.h 554 2004-10-30 00:19:27Z fog $
 */

#ifndef __PSYCOPG_TYPEMOD__
#define __PSYCOPG_TYPEMOD__


/**** the date and time objects ****/

/* type of type-casting functions (both C and Python) */
typedef PyObject *(*datetimeobj_cast_function)(PyObject *);

typedef struct {
    PyObject_HEAD

    PyObject *datetime;
    
    int       type;
#define       PSYCO_DATETIME_TIME       0
#define       PSYCO_DATETIME_DATE       1  
#define       PSYCO_DATETIME_TIMESTAMP  2
} psyco_DateTimeObject;

extern PyTypeObject psyco_DateTimeObject_Type;

/**** the conversion strings ****/
#define PSYCO_DATETIME_TIME_CONV       "'%H:%M:%S'"
#define PSYCO_DATETIME_DATE_CONV       "'%Y-%m-%d'"
#define PSYCO_DATETIME_TIMESTAMP_CONV  "'%Y-%m-%d %H:%M:%S'"


/**** the buffer object ****/

/* type of type-casting functions (both C and Python) */
typedef PyObject *(*bufferobj_cast_function)(PyObject *);

typedef struct {
    PyObject_HEAD

    PyObject  *buffer;
} psyco_BufferObject;

extern PyTypeObject psyco_BufferObject_Type;


/**** the quoted string object ****/

/* type of type-casting functions (both C and Python) */
typedef PyObject *(*quotedstringobj_cast_function)(PyObject *);

typedef struct {
    PyObject_HEAD

    PyObject  *buffer;
} psyco_QuotedStringObject;

extern PyTypeObject psyco_QuotedStringObject_Type;


/**** externally visible functions ****/
extern PyObject *psyco_Date(PyObject *self, PyObject *args);
extern PyObject *psyco_Time(PyObject *self, PyObject *args);
extern PyObject *psyco_Timestamp(PyObject *self, PyObject *args);
extern PyObject *psyco_DateFromTicks(PyObject *self, PyObject *args);
extern PyObject *psyco_TimeFromTicks(PyObject *self, PyObject *args);
extern PyObject *psyco_TimestampFromTicks(PyObject *self, PyObject *args);
extern PyObject *psyco_DateFromMx(PyObject *self, PyObject *args);
extern PyObject *psyco_TimeFromMx(PyObject *self, PyObject *args);
extern PyObject *psyco_TimestampFromMx(PyObject *self, PyObject *args);
extern PyObject *psyco_Binary(PyObject *self, PyObject *args);
extern PyObject *psyco_QuotedString(PyObject *self, PyObject *args);

/* this one is used by the mogrifying code in cursor.c too */
extern PyObject *new_psyco_quotedstringobject(PyObject *buffer);

#endif /* __PSYCOPG_TYPEMOD__ */
