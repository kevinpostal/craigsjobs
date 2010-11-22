#!/bin/sh
srcdir=`dirname $0`
PROJECT="psycopg"
TEST_TYPE=-d
FILE=src


DIE=0

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoconf installed to compile $PROJECT."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
}


if test "$DIE" -eq 1; then
	exit 1
fi

if test -z "$*"; then
	echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
fi

echo Running autoconf...
autoconf

if test "$1" = "--dont-run-configure"; then
  echo "Not running configure on your request. Type './configure' before make."
else
  ./configure "$@" && echo && \
      echo "Now type 'make' to compile the $PROJECT python module."
fi

