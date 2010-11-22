/*
 * Copyright (C) 2001-2002 Federico Di Gregorio <fog@debian.org>
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
 * typeobj.c -- define the type objects
 * $Id: typeobj.c 554 2004-10-30 00:19:27Z fog $
 */

#include "module.h"
#include "typeobj.h"
#include <assert.h>

static PyObject *psyco_DBAPITypeObject_new(PyObject *, PyObject *, PyObject *);
extern PyTypeObject psyco_DBAPITypeObject_Type;


/**** builtin C casting functions ****/

static PyObject *
psyco_INTEGER_cast(PyObject *s)
{
    PyObject *i;
    if (s == Py_None) {Py_INCREF(s); return s;}
    i = PyNumber_Int(s);
    Dprintf("psyco_STRING_cast(): i->refcnt = %d\n", i->ob_refcnt);
    return i;
}

static PyObject *
psyco_LONGINTEGER_cast(PyObject *s)
{
    if (s == Py_None) {Py_INCREF(s); return s;}
    return PyNumber_Long(s);
}

static PyObject *
psyco_FLOAT_cast(PyObject *s)
{
    if (s == Py_None) {Py_INCREF(s); return s;}
    return PyNumber_Float(s);
}

static PyObject *
psyco_STRING_cast(PyObject *s)
{
    if (s == Py_None) {Py_INCREF(s); return s;}
    Py_INCREF(s);
    return s;
}

static PyObject *
psyco_DATE_cast(PyObject *s)
{
    int n, y=0, m=0, d=0;
    int hh=0, mm=0;
    double ss=0.0;
    
    char *str;

    if (s == Py_None) {Py_INCREF(s); return s;}
    str = PyString_AsString(s);
    
    /* check for infinity */
    if (!strcmp(str, "infinity") || !strcmp(str, "-infinity")) {
        if (str[0] == '-') {
            return mxDateTimeP->DateTime_FromDateAndTime(-999998,1,1, 0,0,0);
        }
        else {
            return mxDateTimeP->DateTime_FromDateAndTime(999999,12,31, 0,0,0);
        }
    }
    
    Dprintf("psyco_DATE_cast(): s = %s\n", str);
    n = sscanf(str, "%d-%d-%d %d:%d:%lf", &y, &m, &d, &hh, &mm, &ss);
    Dprintf("psyco_DATE_cast(): date parsed, %d components\n", n);
    
    if (n != 3 && n != 6) {
        PyErr_SetString(DataError, "unable to parse date or timestamp");
        return NULL;
    }
    return mxDateTimeP->DateTime_FromDateAndTime(y, m, d, hh, mm, ss);
}

static PyObject *
psyco_TIME_cast(PyObject *s)
{
    int n, hh=0, mm=0;
    double ss=0.0;
    
    char *str;

    if (s == Py_None) {Py_INCREF(s); return s;}
    str = PyString_AsString(s);
    /* postgres gives us only the time but a DateTime object wants a date too,
       so we use the unix epoch as the current date. */
    
    Dprintf("psyco_TIME_cast(): s = %s\n", str);
    
    n = sscanf(str, "%d:%d:%lf", &hh, &mm, &ss);
    Dprintf("psyco_TIME_cast(): time parsed, %d components\n", n);
    Dprintf("psyco_TIME_cast(): hh = %d, mm = %d, ss = %f\n", hh, mm, ss);
    
    if (n != 3) {
        PyErr_SetString(DataError, "unable to parse time");
        return NULL;
    }

    return mxDateTimeP->DateTimeDelta_FromTime(hh, mm ,ss);
}

static char *
skip_until_space(char *s)
{
    while (*s && *s != ' ') s++;
    return s;
}

static PyObject *
psyco_INTERVAL_cast(PyObject *s)
{
    long years = 0, months = 0, days = 0, denominator = 1;
    double hours = 0.0, minutes = 0.0, seconds = 0.0, hundredths = 0.0;
    double v = 0.0, sign = 1.0;
    int part = 0;
    char *str;

    if (s == Py_None) {Py_INCREF(s); return s;}
    
    str = PyString_AsString(s);
    Dprintf("psyco_INTERVAL_cast(): s = %s\n", str);
    
    while (*str) {
        switch (*str) {

        case '-':
            sign = -1.0;
            break;

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            v = v*10 + (double)*str - (double)'0';
            Dprintf("psyco_INTERVAL_cast(): v = %f\n", v);
            if (part == 6){
                denominator *= 10;
                Dprintf("psyco_INTERVAL_cast(): denominator = %ld\n",
                        denominator);
            }
            break;

        case 'y':
            if (part == 0) {
                years = (long)(v*sign);
                str = skip_until_space(str);
                Dprintf("psyco_INTERVAL_cast(): years = %ld, rest = %s\n",
                        years, str);
                v = 0.0; sign = 1.0; part = 1;
            }
            break;

        case 'm':
            if (part <= 1) {
                months = (long)(v*sign);
                str = skip_until_space(str);
                Dprintf("psyco_INTERVAL_cast(): months = %ld, rest = %s\n",
                        months, str);
                v = 0.0; sign = 1.0; part = 2;
            }
            break;

        case 'd':
            if (part <= 2) {
                days = (long)(v*sign);
                str = skip_until_space(str);
                Dprintf("psyco_INTERVAL_cast(): days = %ld, rest = %s\n",
                        days, str);
                v = 0.0; sign = 1.0; part = 3;
            }
            break;

        case ':':
            if (part <= 3) {
                hours = v;
                Dprintf("psyco_INTERVAL_cast(): hours = %f\n", hours);
                v = 0.0; part = 4;
            }
            else if (part == 4) {
                minutes = v;
                Dprintf("psyco_INTERVAL_cast(): minutes = %f\n", minutes);
                v = 0.0; part = 5;
            }
            break;

        case '.':
            if (part == 5) {
                seconds = v;
                Dprintf("psyco_INTERVAL_cast(): seconds = %f\n", seconds);
                v = 0.0; part = 6;
            }
            break;   

        default:
            break;
        }
        
        str++;
    }

    /* manage last value, be it minutes or seconds or hundredths of a second */
    if (part == 4) {
        minutes = v;
        Dprintf("psyco_INTERVAL_cast(): minutes = %f\n", minutes);
    }
    else if (part == 5) {
        seconds = v;
        Dprintf("psyco_INTERVAL_cast(): seconds = %f\n", seconds);
    }
    else if (part == 6) {
        hundredths = v;
        Dprintf("psyco_INTERVAL_cast(): hundredths = %f\n", hundredths);
        hundredths = hundredths/denominator;
        Dprintf("psyco_INTERVAL_cast(): ACTUAL FRACTIONS = %.20f\n", hundredths);
    }
    
    /* calculates seconds */
    if (sign < 0.0) {
        seconds = - (hundredths + seconds + minutes*60 + hours*3600);
    }
    else {
        seconds += hundredths + minutes*60 + hours*3600;
    }

    /* calculates days */
    days += years*365 + months*30;
    
    Dprintf("psyco_INTERVAL_cast(): days = %ld, seconds = %f\n",
            days, seconds);
    return mxDateTimeP->DateTimeDelta_FromDaysAndSeconds(days, seconds);
}

static PyObject *
psyco_BOOLEAN_cast(PyObject *s)
{
    if (s == Py_None) {Py_INCREF(s); return s;}
    if (PyString_AsString(s)[0] == 't') return PyInt_FromLong(1L);
    else return PyInt_FromLong(0L);
}

/* convert all the \xxx octal sequences to the proper byte value */
static PyObject *
psyco_BINARY_cast(PyObject *s)
{
    char *str, *dstptr, *dststr;
    int len, i;
    PyObject *retstr;

    if (s == Py_None) {Py_INCREF(s); return s;}
   
    str = PyString_AS_STRING(s);
    len = strlen(str);
    dststr = (char*)calloc(len, sizeof(char));
    dstptr = dststr;

    Py_BEGIN_ALLOW_THREADS;
   
    for (i = 0; i < len; i++) {
        if (str[i] == '\\') {
            if ( ++i < len) {
                if (str[i] == '\\') {
                    *dstptr = '\\';
                }
                else {
                    *dstptr = 0;
                    *dstptr |= (str[i++] & 7) << 6;
                    *dstptr |= (str[i++] & 7) << 3;
                    *dstptr |= (str[i] & 7);
                }
            }
        }
        else {
            *dstptr = str[i];
        }
        dstptr++;
    }
    
    Py_END_ALLOW_THREADS;

    Dprintf("Binary: %s, len: %d\n", dststr, dstptr - dststr);
    retstr = PyString_FromStringAndSize(dststr, dstptr - dststr);
    free(dststr);
    return retstr;
}


#define psyco_NUMBER_cast psyco_FLOAT_cast
#define psyco_DATETIME_cast psyco_DATE_cast
#define psyco_ROWID_cast psyco_INTEGER_cast


/*** builtin casting objects (generated by the buildtypes.py script) ****/

#include "typeobj_builtins.c"


/**** the type dictionary and associated functions ****/

/* dictionary of types and default cast object */

PyObject *psyco_types;
PyObject *psyco_default_cast;
PyObject *psyco_binary_cast;

static int psyco_default_cast_type_DEFAULT[] = {0};
static psyco_DBAPIInitList psyco_default_cast_type = {
    "DEFAULT", psyco_default_cast_type_DEFAULT, psyco_STRING_cast};


/* psyco_init_types() - initialize the dictionary and create default types */
int
psyco_init_types(PyObject *md)
{
    int i;
    
    /* create type dictionary and put it in module namespace */
    psyco_types = PyDict_New();
    if (!psyco_types) return -1;
    PyDict_SetItemString(md, "types", psyco_types);

    /* insert the cast types into the 'types' dictionary and register them
     * in the module dictionary */
    for (i = 0; psyco_cast_types[i].name != NULL; i++) {
        psyco_DBAPITypeObject *t;
        Dprintf("psyco_init_types: initializing %s\n",
                psyco_cast_types[i].name);
        if (!(t = (psyco_DBAPITypeObject *)
              new_psyco_typeobject(&(psyco_cast_types[i])))) return -1;
        if (psyco_add_type((PyObject *)t) != 0) return -1;

        PyDict_SetItem(md, t->name, (PyObject *)t);

        /* export binary object */
        if (psyco_cast_types[i].values == psyco_cast_types_BINARY) {
            psyco_binary_cast = (PyObject *)t;
        }
    }

    assert(psyco_binary_cast != NULL);
    
    /* create and save a default cast object (but does not register it) */
    psyco_default_cast = new_psyco_typeobject(&psyco_default_cast_type);
    return 0;
}


/* psyco_add_type() - add a type object to the dictionary */
int
psyco_add_type(PyObject *obj)
{
    psyco_DBAPITypeObject *type = (psyco_DBAPITypeObject *)obj;
    PyObject *val;
    int len, i;

    Dprintf("psyco_add_type: typeobject = %p, values refcnt = %d\n",
            obj, type->values->ob_refcnt);
    len = PyTuple_Size(type->values);
    for (i = 0; i < len; i++) {
        val = PyTuple_GetItem(type->values, i);
        Dprintf("psyco_add_type: val = %ld\n",PyInt_AsLong(val));
        PyDict_SetItem(psyco_types, val, obj);
    }

    return 0;
}


/**** DBAPITypeObject OBJECT DEFINITIONS ****/

/* object member list */

#define OFFSETOF(x) offsetof(psyco_DBAPITypeObject, x)

static struct memberlist psyco_DBAPITypeObject_memberlist[] = {
    {"name", T_OBJECT, OFFSETOF(name), RO},
    {"values", T_OBJECT, OFFSETOF(values), RO},
    {NULL}
};


/**** DBAPITypeObject METHODS ****/

/* the python number interface for the cursor object */

static int
psyco_DBAPITypeObject_coerce(PyObject **pv, PyObject **pw)
{    
    if (PyObject_TypeCheck(*pv, &psyco_DBAPITypeObject_Type)) {
        if (PyInt_Check(*pw)) {
            PyObject *coer, *args;
            args = PyTuple_New(1);
            Py_INCREF(*pw);
            PyTuple_SET_ITEM(args, 0, *pw);
            coer = psyco_DBAPITypeObject_new(NULL, args, NULL);
            *pw = coer;
            Py_DECREF(args);
            Py_INCREF(*pv);
            return 0;
        }
        else if (PyObject_TypeCheck(*pw, &psyco_DBAPITypeObject_Type)){
            Py_INCREF(*pv);
            Py_INCREF(*pw);
            return 0;
        }
    }
    PyErr_SetString(PyExc_TypeError, "psycopg type coercion failed");
    return -1;
}

static PyNumberMethods psyco_DBAPITypeObject_as_number = {
    (binaryfunc)0, 	/*nb_add*/
    (binaryfunc)0,	/*nb_subtract*/
    (binaryfunc)0,	/*nb_multiply*/
    (binaryfunc)0,	/*nb_divide*/
    (binaryfunc)0,	/*nb_remainder*/
    (binaryfunc)0,	/*nb_divmod*/
    (ternaryfunc)0,	/*nb_power*/
    (unaryfunc)0,	/*nb_negative*/
    (unaryfunc)0,	/*nb_positive*/
    (unaryfunc)0,	/*nb_absolute*/
    (inquiry)0,	    /*nb_nonzero*/
    (unaryfunc)0,	/*nb_invert*/
    (binaryfunc)0,	/*nb_lshift*/
    (binaryfunc)0,	/*nb_rshift*/
    (binaryfunc)0,	/*nb_and*/
    (binaryfunc)0,	/*nb_xor*/
    (binaryfunc)0,	/*nb_or*/
    (coercion)psyco_DBAPITypeObject_coerce,	/*nb_coerce*/
    (unaryfunc)0,	/*nb_int*/
    (unaryfunc)0,	/*nb_long*/
    (unaryfunc)0,	/*nb_float*/
    (unaryfunc)0,	/*nb_oct*/
    (unaryfunc)0,	/*nb_hex*/
};


/* the python object interface for the cursor object */

static int
psyco_DBAPITypeObject_cmp(psyco_DBAPITypeObject *self,
                          psyco_DBAPITypeObject *v)
{
    int res;

    if (PyObject_Length(v->values) > 1 && PyObject_Length(self->values) == 1) {
        /* calls itself exchanging the args */
        Dprintf("psyco_DBAPITypeObject_cmp: swapping args\n");
        return psyco_DBAPITypeObject_cmp(v, self);
    }
    res = PySequence_Contains(self->values, PyTuple_GET_ITEM(v->values, 0));
    Dprintf("psyco_DBAPITypeObject_cmp: res = %d\n", res);
    
    if (res < 0) return res;
    else return (res == 1 ? 0 : 1);
}


static struct PyMethodDef psyco_DBAPITypeObject_methods[] = {
    {"__cmp__", (PyCFunction)psyco_DBAPITypeObject_cmp, METH_VARARGS, NULL},
    {NULL, NULL}
};

static PyObject *
psyco_DBAPITypeObject_getattr(psyco_DBAPITypeObject *self, char *name)
{
    PyObject *rv;
	
    rv = PyMember_Get((char *)self, psyco_DBAPITypeObject_memberlist, name);
    if (rv) return rv;
    PyErr_Clear();
    return Py_FindMethod(psyco_DBAPITypeObject_methods,
                         (PyObject *)self, name);
}


static void
psyco_DBAPITypeObject_destroy(psyco_DBAPITypeObject *self)
{
    PyObject *name, *cast, *values;

    values = self->values;
    name = self->name;
    cast = self->pcast;

    Dprintf("psyco_DBAPITypeObject_destroy: value refcnt = %d\n",
            values->ob_refcnt);
    Dprintf("psyco_DBAPITypeObject_destroy: value[0] refcnt = %d\n",
            PyTuple_GET_ITEM(self->values, 0)->ob_refcnt);
    PyObject_Del(self);

    Py_XDECREF(name);
    Py_XDECREF(values);
    Py_XDECREF(cast);
    Dprintf("DBAPITypeObject at %p destroyed\n", self);
}


static PyObject *
psyco_DBAPITypeObject_call(PyObject *self, PyObject *args, PyObject *keywds)
{
    /* when called simply converts from string to an object dependent on the
       type this object represents */
    
    PyObject *string, *res;
    psyco_DBAPITypeObject *me = (psyco_DBAPITypeObject *)self;
    
    Dprintf("psyco_DBAPITypeObject_call: self = %p, args = %p\n", self, args);
    
    if (!PyArg_ParseTuple(args, "O", &string)) {
        return NULL;
    }
    
    Dprintf("psyco_DBAPITypeObject_call: string argument at %p, refcnt = %d\n",
            string, string->ob_refcnt);

    if (me->ccast) {
        Dprintf("psyco_DBAPITypeObject_call: calling C cast function\n");
        res = me->ccast(string);
    }
    else if (me->pcast) {
        PyObject *arg = PyTuple_New(1);
        
        Dprintf("psyco_DBAPITypeObject_call: calling python callable\n");
        PyTuple_SET_ITEM(arg, 0, string);
        Py_INCREF(string);
        res = PyObject_CallObject(me->pcast, arg);
        Dprintf("psyco_DBAPITypeObject_call: res = %p\n", res);
        Py_DECREF(arg);
    }
    else {
        Py_INCREF(Py_None);
        res = Py_None;
    }

    Dprintf("psyco_DBAPITypeObject_call: string argument at %p, refcnt = %d\n",
            string, string->ob_refcnt);
    return res;
}        


static char psyco_DBAPITypeObject__doc__[] = "Object to classify data types.";

PyTypeObject psyco_DBAPITypeObject_Type = {
#if !defined(_WIN32) && !defined(__CYGWIN__)
    PyObject_HEAD_INIT(&PyType_Type)
#else
    PyObject_HEAD_INIT(NULL)
#endif
    0,				                    /*ob_size*/
    "type",         		            /*tp_name*/
    sizeof(psyco_DBAPITypeObject),		/*tp_basicsize*/
    0,				                    /*tp_itemsize*/

    /* methods */
    (destructor)psyco_DBAPITypeObject_destroy,   /*tp_dealloc*/
    (printfunc)0,	                            /*tp_print*/
    (getattrfunc)psyco_DBAPITypeObject_getattr,  /*tp_getattr*/
    (setattrfunc)0,	                            /*tp_setattr*/
    (cmpfunc)psyco_DBAPITypeObject_cmp,		    /*tp_compare*/
    (reprfunc)0,                                /*tp_repr*/
    &psyco_DBAPITypeObject_as_number,		    /*tp_as_number*/
    0,		                                    /*tp_as_sequence*/
    0,		                                    /*tp_as_mapping*/
    (hashfunc)0,		                        /*tp_hash*/
    (ternaryfunc)psyco_DBAPITypeObject_call,     /*tp_call*/
    (reprfunc)0,		                        /*tp_str*/
    
    /* Space for future expansion */
    0L,0L,0L,0L,
    psyco_DBAPITypeObject__doc__  /* Documentation string */
};

static PyObject *
psyco_DBAPITypeObject_new(PyObject *name, PyObject *values, PyObject *cast)
{
    psyco_DBAPITypeObject *obj;

#if defined(_WIN32) || defined(__CYGWIN__)
    /* For MSVC workaround */
    psyco_DBAPITypeObject_Type.ob_type = &PyType_Type;
#endif

    obj = PyObject_NEW(psyco_DBAPITypeObject, &psyco_DBAPITypeObject_Type);
    if (obj == NULL) return NULL;

    Py_INCREF(values);
    obj->values = values;
    
    if (name) {
        Py_INCREF(name);
        obj->name = name;
    }
    else {
        Py_INCREF(Py_None);
        obj->name = Py_None;
    }

    obj->pcast = NULL;
    if (cast && cast != Py_None) {
        Py_INCREF(cast);
        obj->pcast = cast;
    }
    obj->ccast = NULL;
    
    Dprintf("psyco_DBAPITypeObject_new: name = %p, values = %p, "
            "pcast = %p, ccast = %p\n",
            obj->name, obj->values, obj->pcast, obj->ccast);
    Dprintf("psyco_DBAPITypeObject_new: values refcnt = %d\n",
            values->ob_refcnt);
    Dprintf("psyco_DBAPITypeObject_new: object created at %p\n", obj);
    return (PyObject *)obj;
}


/* create a new type object from python */

PyObject *
psyco_DBAPITypeObject_init(PyObject *self, PyObject *args, PyObject *keywds)
{
    PyObject *v, *name, *cast = NULL;
    static char *kwlist[] = {"values", "name", "castobj", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "O!|O!O", kwlist, 
                                     &PyTuple_Type, &v,
                                     &PyString_Type, &name,
                                     &cast)) {
        return NULL;
    }
    
    Dprintf("psyco_DBAPITypeObject_init: arguments parsed\n");
    return psyco_DBAPITypeObject_new(name, v, cast);
}


/* create a new type object from C */

PyObject *
new_psyco_typeobject(psyco_DBAPIInitList *type)
{
    PyObject *tuple;
    psyco_DBAPITypeObject *obj;
    int i, len = 0;

    while (type->values[len] != 0) len++;
    
    tuple = PyTuple_New(len);
    if (!tuple) return NULL;

    Dprintf("new_psyco_typeobject: type tuple created at %p\n", tuple);
    
    for (i = 0; i < len ; i++) {
        PyTuple_SET_ITEM(tuple, i, PyInt_FromLong(type->values[i]));
    }
    
    obj = (psyco_DBAPITypeObject *)
        psyco_DBAPITypeObject_new(PyString_FromString(type->name),
                                  tuple, NULL);
    
    if (obj) {
        obj->ccast = type->cast;
        obj->pcast = NULL;
    }
    return (PyObject *)obj;
}

