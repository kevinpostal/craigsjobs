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
 * typemod.c -- types available to the psycopg module
 * $Id: typemod.c 839 2005-08-22 22:55:21Z fog $
 */

#include "module.h"
#include "typemod.h"
#include <assert.h>

/**** DateTimeObject OBJECT DEFINITIONS ****/

/* object member list */

#define OFFSETOF(x) offsetof(psyco_DateTimeObject, x)

static struct memberlist psyco_DateTimeObject_memberlist[] = {
    {"_o", T_OBJECT, OFFSETOF(datetime), RO},
    {NULL}
};


/**** DateTimeObject METHODS ****/

/* the python object interface for the date and time object */

static PyObject *
psyco_DateTimeObject_str(psyco_DateTimeObject *self)
{
    PyObject *res = NULL;
    char *buffer = NULL;
    
    mxDateTimeObject *obj = (mxDateTimeObject*)self->datetime;

    switch (self->type) {
        
    case 0:
        asprintf(&buffer, "'%02d:%02d:%.6f'",
                 (int)obj->hour, (int)obj->minute, (float)obj->second);
        if (buffer) res = PyString_FromString(buffer);
        break;

    case 1:
        asprintf(&buffer, "'%ld-%02d-%02d'",
                 obj->year, (int)obj->month, (int)obj->day);
        if (buffer) res = PyString_FromString(buffer);
        break;

    case 2:
        asprintf(&buffer, "'%ld-%02d-%02d %02d:%02d:%.6f'",
                 obj->year, (int)obj->month, (int)obj->day,
                 (int)obj->hour, (int)obj->minute, (float)obj->second);
        if (buffer) res = PyString_FromString(buffer);
        break;
    }

    if (buffer) free(buffer);
    return res;
}

static struct PyMethodDef psyco_DateTimeObject_methods[] = {
    /*{"datetime", (PyCFunction)psyco_DateTimeObject_datetime,
      METH_VARARGS, NULL},*/
    {NULL, NULL}
};

static PyObject *
psyco_DateTimeObject_getattr(psyco_DateTimeObject *self, char *name)
{
    PyObject *rv;
	
    rv = PyMember_Get((char *)self, psyco_DateTimeObject_memberlist, name);
    if (rv) return rv;
    PyErr_Clear();
    return Py_FindMethod(psyco_DateTimeObject_methods, (PyObject *)self, name);
}


static void
psyco_DateTimeObject_destroy(psyco_DateTimeObject *self)
{
    Py_XDECREF(self->datetime);
    PyObject_Del(self);
    Dprintf("DateTimeObject at %p destroyed\n", self);
}


static char psyco_DateTimeObject__doc__[] =
"Object to pass (in a portable way) dates and times to the database.";

PyTypeObject psyco_DateTimeObject_Type = {
#if !defined(_WIN32) && !defined(__CYGWIN__)
    PyObject_HEAD_INIT(&PyType_Type)
#else
    PyObject_HEAD_INIT(NULL)
#endif
    0,				                    /*ob_size*/
    "datetime",                         /*tp_name*/
    sizeof(psyco_DateTimeObject),		/*tp_basicsize*/
    0,				                    /*tp_itemsize*/

    /* methods */
    (destructor)psyco_DateTimeObject_destroy,   /*tp_dealloc*/
    (printfunc)0,	                            /*tp_print*/
    (getattrfunc)psyco_DateTimeObject_getattr,  /*tp_getattr*/
    (setattrfunc)0,	                            /*tp_setattr*/
    (cmpfunc)0,                                 /*tp_compare*/
    (reprfunc)0,                                /*tp_repr*/
    0,                              		    /*tp_as_number*/
    0,		                                    /*tp_as_sequence*/
    0,		                                    /*tp_as_mapping*/
    (hashfunc)0,		                        /*tp_hash*/
    (ternaryfunc)0,                             /*tp_call*/
    (reprfunc)psyco_DateTimeObject_str,         /*tp_str*/
    
    /* Space for future expansion */
    0L,0L,0L,0L,
    psyco_DateTimeObject__doc__  /* Documentation string */
};

static PyObject *
new_psyco_datetimeobject(PyObject *datetime, int type)
{
    psyco_DateTimeObject *obj;

#if defined(_WIN32) || defined(__CYGWIN__)
    /* For MSVC workaround */
    psyco_DateTimeObject_Type.ob_type = &PyType_Type;
#endif

    obj = PyObject_NEW(psyco_DateTimeObject, &psyco_DateTimeObject_Type);
    if (obj == NULL) return NULL;

    obj->datetime = datetime;
    obj->type = type;

    Dprintf("new_psyco_datetimeobject: object of type %d created at %p\n",
            type, obj);
    return (PyObject *)obj;
}


/**** BufferObject OBJECT DEFINITIONS ****/

/* object member list */

#undef OFFSETOF
#define OFFSETOF(x) offsetof(psyco_BufferObject, x)

static struct memberlist psyco_BufferObject_memberlist[] = {
    {NULL}
};


/**** BufferObject METHODS ****/

/* the python object interface for the buffer object */

static PyObject *
psyco_BufferObject_str(psyco_BufferObject *self)
{
    Py_INCREF(self->buffer);
    return self->buffer;
}

static struct PyMethodDef psyco_BufferObject_methods[] = {
    /*{"datetime", (PyCFunction)psyco_BufferObject_datetime,
      METH_VARARGS, NULL},*/
    {NULL, NULL}
};

static PyObject *
psyco_BufferObject_getattr(psyco_BufferObject *self, char *name)
{
    PyObject *rv;
	
    rv = PyMember_Get((char *)self, psyco_BufferObject_memberlist, name);
    if (rv) return rv;
    PyErr_Clear();
    return Py_FindMethod(psyco_BufferObject_methods, (PyObject *)self, name);
}


static void
psyco_BufferObject_destroy(psyco_BufferObject *self)
{
    Py_DECREF(self->buffer);
    PyObject_Del(self);
    Dprintf("psyco_BufferObject_destroy(): bufferobject at %p destroyed\n", self);
}


static char psyco_BufferObject__doc__[] =
"Object to pass (in a portable way) binary data to the database.";

PyTypeObject psyco_BufferObject_Type = {
#if !defined(_WIN32) && !defined(__CYGWIN__)
    PyObject_HEAD_INIT(&PyType_Type)
#else
    PyObject_HEAD_INIT(NULL)
#endif
    0,				                    /*ob_size*/
    "buffer",                           /*tp_name*/
    sizeof(psyco_BufferObject),	     	/*tp_basicsize*/
    0,				                    /*tp_itemsize*/

    /* methods */
    (destructor)psyco_BufferObject_destroy,     /*tp_dealloc*/
    (printfunc)0,	                            /*tp_print*/
    (getattrfunc)psyco_BufferObject_getattr,    /*tp_getattr*/
    (setattrfunc)0,	                            /*tp_setattr*/
    (cmpfunc)0,                                 /*tp_compare*/
    (reprfunc)0,                                /*tp_repr*/
    0,                              		    /*tp_as_number*/
    0,		                                    /*tp_as_sequence*/
    0,		                                    /*tp_as_mapping*/
    (hashfunc)0,		                        /*tp_hash*/
    (ternaryfunc)0,                             /*tp_call*/
    (reprfunc)psyco_BufferObject_str,           /*tp_str*/
    
    /* Space for future expansion */
    0L,0L,0L,0L,
    psyco_BufferObject__doc__                   /* Documentation string */
};

static PyObject *
new_psyco_bufferobject(PyObject *buffer)
{
    psyco_BufferObject *obj;
    unsigned char *original, *quoted, *chptr, *newptr;
    int i, len, space, new_space;

#if defined(_WIN32) || defined(__CYGWIN__)
    /* For MSVC workaround */
    psyco_BufferObject_Type.ob_type = &PyType_Type;
#endif

    obj = PyObject_NEW(psyco_BufferObject, &psyco_BufferObject_Type);
    if (obj == NULL) return NULL;

    original = (unsigned char*)PyString_AS_STRING(buffer);
    len = PyString_GET_SIZE(buffer);
    space = len + 2;

    Py_BEGIN_ALLOW_THREADS;

    quoted = (unsigned char*)calloc(space, sizeof(char));
    if (quoted == NULL) return NULL;

    chptr = quoted;
    *chptr = '\'';
    chptr++;

    for (i=0; i < len; i++) {
        if (chptr - quoted > space - 6) {
            new_space  =  space * ((space) / (i + 1)) + 2 + 6;
            if (new_space - space < 1024) space += 1024;
            else space = new_space;
            newptr = (unsigned char *)realloc(quoted, space);
            if (newptr == NULL) {
                free(quoted);
                return NULL;
            }
            /*chptr has to be moved to the new location*/
            chptr = newptr + (chptr - quoted);
            quoted = newptr;
            Dprintf("new_psyco_bufferobject(): reallocated %i bytes at %p \n",
                    space, quoted);
        }
        if (original[i]) {
            if (original[i] >= ' ' && original[i] <= '~') {
                if (original[i] == '\'') {
                    *chptr = '\\';
                    chptr++;
                    *chptr = '\'';
                    chptr++;
                }
                else if (original[i] == '\\') {
                    memcpy(chptr, "\\\\\\\\", 4);
                    chptr += 4;
                }
                else {
                    /* leave it as it is if ascii printable */
                    *chptr = original[i];
                    chptr++;
                }
            }
            else {
                unsigned char c;
                
                /* escape to octal notation \nnn */
                *chptr++ = '\\';
                *chptr++ = '\\';
                c = original[i];
                *chptr = ((c >> 6) & 0x07) + 0x30;
                chptr++;
                *chptr = ((c >> 3) & 0x07) + 0x30;
                chptr++;
                *chptr = (c & 0x07) + 0x30;
                chptr++;
            }
        }
        else {
            /* escape null as \\000 */
            memcpy(chptr, "\\\\000", 5);
            chptr += 5;
        }
    }
    *chptr = '\'';

    Py_END_ALLOW_THREADS;

    Dprintf("new_psyco_bufferobject(): quoted = %s\n", quoted);
    obj->buffer = PyString_FromStringAndSize((char*)quoted, 
                                             chptr - quoted + 1 );
    free(quoted);
    
    Dprintf("new_psyco_bufferobject: object created at %p\n", obj);
    return (PyObject *)obj;
}


/**** QuotedStringObject OBJECT DEFINITIONS ****/

/* object member list */

#undef OFFSETOF
#define OFFSETOF(x) offsetof(psyco_QuotedStringObject, x)

static struct memberlist psyco_QuotedStringObject_memberlist[] = {
    {NULL}
};


/**** QuotedStringObject METHODS ****/

/* the python object interface for the buffer object */

static PyObject *
psyco_QuotedStringObject_str(psyco_QuotedStringObject *self)
{
    Py_INCREF(self->buffer);
    return self->buffer;
}

static struct PyMethodDef psyco_QuotedStringObject_methods[] = {
    {NULL, NULL}
};

static PyObject *
psyco_QuotedStringObject_getattr(psyco_BufferObject *self, char *name)
{
    PyObject *rv;
	
    rv = PyMember_Get((char *)self, psyco_QuotedStringObject_memberlist, name);
    if (rv) return rv;
    PyErr_Clear();
    return Py_FindMethod(psyco_QuotedStringObject_methods,
                         (PyObject *)self, name);
}


static void
psyco_QuotedStringObject_destroy(psyco_QuotedStringObject *self)
{
    Dprintf("psyco_QuotedStringObject_destroy: "
            "self->buffer->refcnt = %d\n", self->buffer->ob_refcnt);
    Py_DECREF(self->buffer);
    PyObject_Del(self);
    Dprintf("psyco_QuotedStringObject_destroy: "
            "quotedstringobject at %p destroyed\n", self);
}


static char psyco_QuotedStringObject__doc__[] =
"A string that quotes itself following PostgreSQL quoting convention.";

PyTypeObject psyco_QuotedStringObject_Type = {
#if !defined(_WIN32) && !defined(__CYGWIN__)
    PyObject_HEAD_INIT(&PyType_Type)
#else
    PyObject_HEAD_INIT(NULL)
#endif
    0,				                    /*ob_size*/
    "buffer",                           /*tp_name*/
    sizeof(psyco_QuotedStringObject),   /*tp_basicsize*/
    0,				                    /*tp_itemsize*/

    /* methods */
    (destructor)psyco_QuotedStringObject_destroy,     /*tp_dealloc*/
    (printfunc)0,	                                  /*tp_print*/
    (getattrfunc)psyco_QuotedStringObject_getattr,    /*tp_getattr*/
    (setattrfunc)0,	                            /*tp_setattr*/
    (cmpfunc)0,                                 /*tp_compare*/
    (reprfunc)0,                                /*tp_repr*/
    0,                              		    /*tp_as_number*/
    0,		                                    /*tp_as_sequence*/
    0,		                                    /*tp_as_mapping*/
    (hashfunc)0,		                        /*tp_hash*/
    (ternaryfunc)0,                             /*tp_call*/
    (reprfunc)psyco_QuotedStringObject_str,     /*tp_str*/
    
    /* Space for future expansion */
    0L,0L,0L,0L,
    psyco_QuotedStringObject__doc__             /* Documentation string */
};

PyObject *
new_psyco_quotedstringobject(PyObject *buffer)
{
    psyco_QuotedStringObject *obj;
    char *original, *quoted;
    int i, j, len;

#if defined(_WIN32) || defined(__CYGWIN__)
    /* For MSVC workaround */
    psyco_QuotedStringObject_Type.ob_type = &PyType_Type;
#endif

    obj = PyObject_NEW(psyco_QuotedStringObject,
                       &psyco_QuotedStringObject_Type);
    if (obj == NULL) return NULL;

    original = PyString_AS_STRING(buffer);
    len = PyString_GET_SIZE(buffer);

    Dprintf("new_psyco_quotedstringobject: len = %d\n", len);
    
    quoted = (char*)malloc(len*2+3);
    if (quoted == NULL) return NULL;

    for (i=0, j=1; i<len; i++) {
        switch(original[i]) {

        case '\'':
            quoted[j++] = '\'';
            quoted[j++] = '\'';
            break;

        case '\\':
            quoted[j++] = '\\';
            quoted[j++] = '\\';
            break;

        case '\0':
            /* do nothing, embedded \0 are discarded */
            break;
            
        default:
            quoted[j++] = original[i];
        }
    }
    quoted[0] = '\'';
    quoted[j++] = '\'';
    quoted[j] = '\0';

    /* are we overwriting someone else memory? */
    assert(j < len*2+3);
        
    Dprintf("new_psyco_quotedstringobject: quoted = %s\n", quoted);

    obj->buffer = PyString_FromStringAndSize(quoted, j);
    free(quoted);
    
    Dprintf("new_psyco_quotedstringobject: object created at %p\n", obj);
    return (PyObject *)obj;
}


/**** PYTHON TO POSTGRESQL CONVERSION OBJECTS ****/

PyObject *
psyco_Date(PyObject *self, PyObject *args) 
{
	PyObject *datetime; /* datetime object */
	long year;
	int month, day;
	
	if (!PyArg_ParseTuple(args, "lii", &year, &month, &day))
	    return NULL;
					 
	if (!(datetime =
	      mxDateTimeP->DateTime_FromDateAndTime(year, month, day, 0, 0, 0.0)))
	    return NULL;
	
    return new_psyco_datetimeobject(datetime, PSYCO_DATETIME_DATE);
}

PyObject *
psyco_Time(PyObject *self, PyObject *args)
{
	PyObject *datetime; /* datetime object */
	int hours, minutes=0;
	double seconds=0.0;
	
	if (!PyArg_ParseTuple(args, "iid", &hours, &minutes, &seconds))
	    return NULL;
					 
    if (!(datetime=
	      mxDateTimeP->DateTimeDelta_FromTime(hours, minutes, seconds)))
	    return NULL;
	
    return new_psyco_datetimeobject(datetime, PSYCO_DATETIME_TIME);
}

PyObject *
psyco_Timestamp(PyObject *self, PyObject *args)
{
	PyObject *datetime; /* datetime object */
	long year;
	int month, day;
	int hour=0, minute=0; /* default to midnight */
	double second=0.0;
	
	if (!PyArg_ParseTuple(args, "lii|iid",
			      &year, &month, &day,
			      &hour, &minute, &second))
	    return NULL;
					 
    if (!(datetime=
	      mxDateTimeP->DateTime_FromDateAndTime(year, month, day,
                                              hour, minute, second)))
	    return NULL;
	
	return new_psyco_datetimeobject(datetime, PSYCO_DATETIME_TIMESTAMP);
}

PyObject *
psyco_DateFromTicks(PyObject *self, PyObject *args)
{
    PyObject *datetime; /* datetime object */
    double ticks;
    long year;
    int month, day;

    if (!PyArg_ParseTuple(args,"d", &ticks))
        return NULL;
					 
    if (!(datetime = mxDateTimeP->DateTime_FromTicks(ticks)))
        return NULL;

    if (mxDateTimeP->DateTime_BrokenDown((mxDateTimeObject *)datetime,
                                       &year, &month, &day,
                                       (int *)NULL, (int *)NULL,
                                       (double *)NULL) == -1)
        return NULL;
    
    /* reset hour, minute, second to 00:00:00.0 (midnight) */
    if (!(datetime =
          mxDateTimeP->DateTime_FromDateAndTime(year, month, day, 0, 0, 0.0)))
        return NULL;
    
    return new_psyco_datetimeobject(datetime, PSYCO_DATETIME_DATE);
}

PyObject *
psyco_TimeFromTicks(PyObject *self, PyObject *args)
{
    PyObject *datetime; /* datetime object */
    double ticks;
    int hours, minutes;
    double seconds;

    if (!PyArg_ParseTuple(args,"d", &ticks))
        return NULL;
					 
    if (!(datetime = mxDateTimeP->DateTime_FromTicks(ticks)))
        return NULL;

    if (mxDateTimeP->DateTime_BrokenDown((mxDateTimeObject *)datetime,
                                       (long *)NULL, (int *)NULL, (int *)NULL,
                                       &hours, &minutes, &seconds) == -1)
        return NULL;
    
    /* consider only Delta using hours, minutes, seconds */
    if (!(datetime =
          mxDateTimeP->DateTimeDelta_FromTime(hours,minutes, seconds)))
        return NULL;
    
    return new_psyco_datetimeobject(datetime, PSYCO_DATETIME_TIME);
}

PyObject *
psyco_TimestampFromTicks(PyObject *self, PyObject *args)
{
    PyObject *datetime; /* datetime object */
    double ticks;

    if (!PyArg_ParseTuple(args, "d", &ticks))
        return NULL;
					 
    if (!(datetime = mxDateTimeP->DateTime_FromTicks(ticks)))
        return NULL;

    return new_psyco_datetimeobject(datetime, PSYCO_DATETIME_TIMESTAMP);
}

PyObject *
psyco_DateFromMx(PyObject *self, PyObject *args)
{
    PyObject *o;

    if (!PyArg_ParseTuple(args, "O!", mxDateTimeP->DateTime_Type, &o))
        return NULL;
    Py_INCREF(o);
    return new_psyco_datetimeobject(o, PSYCO_DATETIME_DATE);
}

PyObject *
psyco_TimeFromMx(PyObject *self, PyObject *args)
{
    PyObject *o;

    if (!PyArg_ParseTuple(args, "O!", mxDateTimeP->DateTime_Type, &o))
        return NULL;
    Py_INCREF(o);
    return new_psyco_datetimeobject(o, PSYCO_DATETIME_TIME);
}

PyObject *
psyco_TimestampFromMx(PyObject *self, PyObject *args)
{
    PyObject *o;

    if (!PyArg_ParseTuple(args, "O!", mxDateTimeP->DateTime_Type, &o))
        return NULL;
    Py_INCREF(o);
    return new_psyco_datetimeobject(o, PSYCO_DATETIME_TIMESTAMP);
}


PyObject *
psyco_Binary(PyObject *self, PyObject *args)
{
    PyObject *str;

    if (!PyArg_ParseTuple(args, "O!", &PyString_Type, &str))
        return NULL;
    return new_psyco_bufferobject(str);
}

PyObject *
psyco_QuotedString(PyObject *self, PyObject *args)
{
    PyObject *str;

    if (!PyArg_ParseTuple(args, "O!", &PyString_Type, &str))
        return NULL;
    return new_psyco_quotedstringobject(str);
}


