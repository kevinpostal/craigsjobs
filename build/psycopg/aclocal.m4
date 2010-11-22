dnl Macros to help with configuring Python extensions via autoconf.
dnl  Copyright (C) 1998,  James Henstridge <james@daa.com.au>
dnl
dnl Distribute under the same rules as Autoconf itself.

dnl call all the functions needed for useing the Makefile.pre.in (except for
dnl PY_OUTPUT)  Requires no arguments.
AC_DEFUN(PY_INIT,
[AC_REQUIRE([PY_PROG_PYTHON])
AC_REQUIRE([PY_PYTHON_VERSION])
AC_REQUIRE([PY_PYTHON_PREFIX])
AC_REQUIRE([PY_PYTHON_EXEC_PREFIX])
AC_REQUIRE([PY_PYTHON_MAKE_VARS])
AC_REQUIRE([PY_CHECK_INSTALL])
AC_REQUIRE([PY_LIB_DIR])
AC_REQUIRE([PY_MOD_DIR])
])

dnl Check for location of python.
AC_DEFUN(PY_PROG_PYTHON,
[AC_ARG_WITH(python,
[  --with-python=CMD       name of python executable],,[with_python=no])
if test "x$with_python" != xno && test -x "$with_python"; then
  AC_MSG_CHECKING(for python)
  PYTHON="$with_python"
  AC_MSG_RESULT($PYTHON)
  AC_SUBST(PYTHON)
else
  AC_PATH_PROG(PYTHON, python, python)
fi])

dnl Check for python version
AC_DEFUN(PY_PYTHON_VERSION,
[AC_REQUIRE([PY_PROG_PYTHON])
AC_ARG_WITH(python-version,
[  --with-python-version=VERSION
                          overide guessed python version],,
[with_python_version=no])
if test "x$with_python_version" != xno; then
  PYTHON_VERSION="$with_python_version"
fi
AC_MSG_CHECKING(python version)
if test -z "$PYTHON_VERSION"; then
  AC_CACHE_VAL(py_cv_python_version,
  [changequote((_,_))
  py_cv_python_version=`$PYTHON -c 'import sys; print sys.version[:3]'`
  changequote([,])])
else
  py_cv_python_version="$PYTHON_VERSION"
fi
PYTHON_VERSION="$py_cv_python_version"
AC_MSG_RESULT($PYTHON_VERSION)
VERSION=$PYTHON_VERSION
AC_SUBST(VERSION)
AC_SUBST(PYTHON_VERSION)
])

dnl Check python installation prefix
AC_DEFUN(PY_PYTHON_PREFIX,
[AC_REQUIRE([PY_PROG_PYTHON])
AC_ARG_WITH(python-prefix,
[  --with-python-prefix=DIR
                          override the prefix for python],,
[with_python_prefix=no])
if test "x$with_python_prefix" != xno; then
  PYTHON_PREFIX="$with_python_prefix"
fi
AC_MSG_CHECKING(python installation prefix)
if test -z "$PYTHON_PREFIX"; then
  AC_CACHE_VAL(py_cv_python_prefix,
  [py_cv_python_prefix=`$PYTHON -c 'import sys; print sys.prefix'`])
else
  py_cv_python_prefix="$PYTHON_PREFIX"
fi
PYTHON_PREFIX="$py_cv_python_prefix"
AC_MSG_RESULT($PYTHON_PREFIX)
AC_SUBST(PYTHON_PREFIX)
prefix=$PYTHON_PREFIX
])

dnl Check python installation exec_prefix
AC_DEFUN(PY_PYTHON_EXEC_PREFIX,
[AC_REQUIRE([PY_PROG_PYTHON])
AC_ARG_WITH(python-prefix,
[  --with-python-exec-prefix=DIR
                          override the exec prefix for python],,
[with_python_exec_prefix=no])
if test "x$with_python_exec_prefix" != xno; then
  PYTHON_EXEC_PREFIX="$with_python_exec_prefix"
fi
AC_MSG_CHECKING(python installation exec_prefix)
if test -z "$PYTHON_EXEC_PREFIX"; then
  AC_CACHE_VAL(py_cv_python_exec_prefix,
  [py_cv_python_exec_prefix=`$PYTHON -c 'import sys; print sys.exec_prefix'`])
else
  py_cv_python_exec_prefix="$PYTHON_EXEC_PREFIX"
fi
PYTHON_EXEC_PREFIX="$py_cv_python_exec_prefix"
AC_MSG_RESULT($PYTHON_EXEC_PREFIX)
AC_SUBST(PYTHON_EXEC_PREFIX)
exec_prefix=$PYTHON_EXEC_PREFIX
])

dnl get the relevant information from the python library Makefile
AC_DEFUN(PY_PYTHON_MAKE_VARS,
[AC_REQUIRE([PY_PYTHON_VERSION])
AC_REQUIRE([PY_PYTHON_EXEC_PREFIX])
AC_MSG_CHECKING(definitions in Python library makefile)
AC_CACHE_VAL(py_cv_python_makefile_vars,
[py_makefile=`$PYTHON -c 'from distutils import sysconfig; print sysconfig.get_makefile_filename()'`
dnl set defaults for these variables so they work with Python 1.4
py_cv_python_makefile_LINKCC=''
py_cv_python_makefile_SGI_ABI=''
py_cv_python_makefile_LDLAST=''
py_cv_python_makefile_SET_CCC=''
changequote((_,_))
eval `sed -n \
-e '/^CC=/ s/CC=[ 	]*\(.*\)/py_cv_python_makefile_CC='\''\1'\''/p' \
-e '/^OPT=/ s/OPT=[ 	]*\(.*\)/py_cv_python_makefile_OPT='\''\1'\''/p' \
-e '/^LDFLAGS=/ s/LDFLAGS=[ 	]*\(.*\)/py_cv_python_makefile_LDFLAGS='\''\1'\''/p' \
-e '/^DEFS=/ s/DEFS=[ 	]*\(.*\)/py_cv_python_makefile_DEFS='\''\1'\''/p' \
-e '/^LIBS=/ s/LIBS=[ 	]*\(.*\)/py_cv_python_makefile_LIBS='\''\1'\''/p' \
-e '/^LIBM=/ s/LIBM=[  	]*\(.*\)/py_cv_python_makefile_LIBM='\''\1'\''/p' \
-e '/^LIBC=/ s/LIBC=[ 	]*\(.*\)/py_cv_python_makefile_LIBC='\''\1'\''/p' \
-e '/^RANLIB=/ s/RANLIB=[ 	]*\(.*\)/py_cv_python_makefile_RANLIB='\''\1'\''/p' \
-e '/^MACHDEP=/ s/MACHDEP=[ 	]*\(.*\)/py_cv_python_makefile_MACHDEP='\''\1'\''/p' \
-e '/^SO=/ s/SO=[ 	]*\(.*\)/py_cv_python_makefile_SO='\''\1'\''/p' \
-e '/^LDSHARED=/ s/LDSHARED=[ 	]*\(.*\)/py_cv_python_makefile_LDSHARED='\''\1'\''/p' \
-e '/^CCSHARED=/ s/CCSHARED=[ 	]*\(.*\)/py_cv_python_makefile_CCSHARED='\''\1'\''/p' \
-e '/^LINKFORSHARED=/ s/LINKFORSHARED=[ 	]*\(.*\)/py_cv_python_makefile_LINKFORSHARED='\''\1'\''/p' \
-e '/^prefix=/ s/prefix=[ 	]*\(.*\)/py_cv_python_makefile_PREFIX='\''\1'\''/p' \
-e '/^exec_prefix=/ s/exec_prefix=[ 	]*\(.*\)/py_cv_python_makefile_EXEC_PREFIX='\''\1'\''/p' \
-e '/^LINKCC=/ s/LINKCC=[ 	]*\(.*\)/py_cv_python_makefile_LINKCC='\''\1'\''/p' \
-e '/^SGI_ABI=/ s/SGI_ABI=[ 	]*\(.*\)/py_cv_python_makefile_SGI_ABI='\''\1'\''/p' \
-e '/^LDLAST=/ s/LDLAST=[ 	]*\(.*\)/py_cv_python_makefile_LDLAST='\''\1'\''/p' \
-e '/^CCC/ s/CCC=[ 	]*\(.*\)/py_cv_python_makefile_SET_CCC='\''CCC=\1'\''/p' \
$py_makefile`
changequote([,])
py_cv_python_makefile_vars=found
])
CC="$py_cv_python_makefile_CC"
OPT="$OPT $py_cv_python_makefile_OPT"
LDFLAGS="$LDFLAGS $py_cv_python_makefile_LDFLAGS"
DEFS="$py_cv_python_makefile_DEFS"
LIBS="$py_cv_python_makefile_LIBS"
LIBM="$py_cv_python_makefile_LIBM"
LIBC="$py_cv_python_makefile_LIBC"
RANLIB="$py_cv_python_makefile_RANLIB"
MACHDEP="$py_cv_python_makefile_MACHDEP"
SO="$py_cv_python_makefile_SO"
LDSHARED="$py_cv_python_makefile_LDSHARED"
CCSHARED="$py_cv_python_makefile_CCSHARED"
LINKFORSHARED="$py_cv_python_makefile_LINKFORSHARED"
PREFIX="$py_cv_python_makefile_PREFIX"
EXEC_PREFIX="$py_cv_python_makefile_EXEC_PREFIX"
LINKCC="$py_cv_python_makefile_LINKCC"
SGI_ABI="$py_cv_python_makefile_SGI_ABI"
LDLAST="$py_cv_python_makefile_LDLAST"
SET_CCC="$py_cv_python_makefile_SET_CCC"
changequote((_,_))
echo "$DEFS" | sed \
-e 's/-D\([^ 	=]*\)=\([^ 	]*\)[ 	]*/#define \1 \2\
/g' \
-e 's/-D\([^ 	=]*\)[ 	]*/#define \1 1\
/g' >> confdefs.h
changequote([,])
AC_SUBST(CC)
AC_SUBST(OPT)
AC_SUBST(LDFLAGS)
AC_SUBST(DEFS)
AC_SUBST(LIBS)
AC_SUBST(LIBM)
AC_SUBST(LIBC)
AC_SUBST(RANLIB)
AC_SUBST(MACHDEP)
AC_SUBST(SO)
AC_SUBST(LDSHARED)
AC_SUBST(CCSHARED)
AC_SUBST(LINKFORSHARED)
AC_SUBST(PREFIX)
AC_SUBST(EXEC_PREFIX)
AC_SUBST(LINKCC)
AC_SUBST(SGI_ABI)
AC_SUBST(LDLAST)
AC_SUBST(SET_CCC)
AC_MSG_RESULT(done)
])

dnl Find the install program, without making the user include install-sh in
dnl their package -- most systems have it, and a copy of install-sh comes
dnl with newer (>=1.5) python distributions
AC_DEFUN(PY_CHECK_INSTALL,
[AC_REQUIRE([PY_PYTHON_VERSION])
AC_REQUIRE([PY_PYTHON_EXEC_PREFIX])
if test -x "$PYTHON_EXEC_PREFIX/lib/python$PYTHON_VERSION/config/install-sh"; then
  INSTALL="$PYTHON_EXEC_PREFIX/lib/python$PYTHON_VERSION/config/install-sh"
  AC_SUBST(INSTALL)
else
  AC_PATH_PROG(INSTALL, install, install)
fi])

dnl find the python module library (makes sure directory is in python's search
dnl path).
AC_DEFUN(PY_LIB_DIR,
[AC_REQUIRE([PY_PROG_PYTHON])
AC_REQUIRE([PY_PYTHON_PREFIX])
AC_REQUIRE([PY_PYTHON_VERSION])
AC_MSG_CHECKING(location of python library)
AC_CACHE_VAL(py_cv_python_library_dir, dnl
[py_cv_python_library_dir=`$PYTHON -c "
import sys, os, os.path
sp = list(())
for p in sys.path:
  sp.append(os.path.normpath(p))
for dir in ('/lib/site-python', '/lib/python${PYTHON_VERSION}/site-packages',
      '/lib/python${PYTHON_VERSION}'):
  p = os.path.normpath(sys.prefix+dir)
  if os.path.isdir(p) and p in sp:
    print dir
    break"`])
PYTHON_LIBRARY_DIR="\$(prefix)$py_cv_python_library_dir"
AC_MSG_RESULT($PYTHON_LIBRARY_DIR)
AC_SUBST(PYTHON_LIBRARY_DIR)
])

dnl find the python shared modules directory (makes sure directory is in
dnl python's search path)
AC_DEFUN(PY_MOD_DIR,
[AC_REQUIRE([PY_PROG_PYTHON])
AC_REQUIRE([PY_PYTHON_PREFIX])
AC_REQUIRE([PY_PYTHON_VERSION])
AC_MSG_CHECKING(location of python shared modules)
AC_CACHE_VAL(py_cv_python_module_dir, dnl
[py_cv_python_module_dir=`$PYTHON -c "
import sys, os
for dir in ('/lib64/python${PYTHON_VERSION}/site-packages',
            '/lib64/python${PYTHON_VERSION}/lib-dynload',
            '/lib64/python${PYTHON_VERSION}/sharedmodules',
            '/lib64/site-python',
            '/lib64/python${PYTHON_VERSION}',
            '/lib/python${PYTHON_VERSION}/site-packages',
            '/lib/python${PYTHON_VERSION}/lib-dynload',
            '/lib/python${PYTHON_VERSION}/sharedmodules',
            '/lib/site-python',
            '/lib/python${PYTHON_VERSION}'):
  if os.path.isdir(sys.exec_prefix+dir) and (sys.exec_prefix+dir) in sys.path:
    print dir
    break"`])
PYTHON_MODULE_DIR="\$(exec_prefix)$py_cv_python_module_dir"
AC_MSG_RESULT($PYTHON_MODULE_DIR)
AC_SUBST(PYTHON_MODULE_DIR)
])

dnl make FILE from FILE.pre.in using config.status and makesetup
dnl   PY_OUTPUT(Makefile, Setup ..., [others])
dnl where `Makefile' is the base of the Makefile.pre.in file, the second
dnl argument gives Setup.in type files (without the .in), and the third
dnl argument gives extra files you want to expand @var@ macros in.
AC_DEFUN(PY_OUTPUT,
[AC_REQUIRE([PY_INIT])
AC_OUTPUT($2 $3 $1.pre,dnl
[echo creating $1
$MAKESETUP -m $1.pre -c - $2],dnl
[MAKESETUP=`$PYTHON -c 'from distutils import sysconfig; parts=sysconfig.get_makefile_filename().split("/")[[:-1]]; parts.append("makesetup"); print "/".join(parts)'`
])
])

dnl ----------------------------------------------------------------------
dnl These functions are used similar to AC_CHECK_LIB and associates.

dnl PY_CHECK_MOD(MODNAME [,SYMBOL [,ACTION-IF-FOUND [,ACTION-IF-NOT-FOUND]]])
dnl Check if a module containing a given symbol is visible to python.
AC_DEFUN(PY_CHECK_MOD,
[AC_REQUIRE([PY_PROG_PYTHON])
py_mod_var=`echo $1['_']$2 | sed 'y%./+-%__p_%'`
AC_MSG_CHECKING(for ifelse([$2],[],,[$2 in ])python module $1)
AC_CACHE_VAL(py_cv_mod_$py_mod_var, [
if $PYTHON -c 'import $1 ifelse([$2],[],,[; $1.$2])' 1>&AC_FD_CC 2>&AC_FD_CC; then
  eval "py_cv_mod_$py_mod_var=yes"
else
  eval "py_cv_mod_$py_mod_var=no"
fi
])
py_val=`eval "echo \`echo '$py_cv_mod_'$py_mod_var\`"`
if test "x$py_val" != xno; then
  AC_MSG_RESULT(yes)
  ifelse([$3], [],, [$3
])dnl
else
  AC_MSG_RESULT(no)
  ifelse([$4], [],, [$4
])dnl
fi
])

