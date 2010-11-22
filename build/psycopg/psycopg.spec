# To recompile for a different version of python:
# (ie rpm --rebuild --define 'python 2.4' psycopg-1.1.21.src.rpm)
#
%{!?python:%define python 2.3}
%define name psycopg
%define version 1.1.21
%define release py%{python}_1

Summary: a PostgreSQL database adapter for Python
Name: %{name}
Version: %{version}
Release: %{release}
Source0: %{name}-%{version}.tar.gz
License: GNU GPL 
Group: Applications/Databases
BuildRoot: %{_tmppath}/%{name}-buildroot
Prefix: %{_prefix}
Packager: William K. Volkman <development@netshark.com>
Url: http://www.initd.org/software/initd/psycopg
BuildRequires: python-devel postgresql-devel mx
Requires: python mx postgresql-libs

%description
psycopg is a PostgreSQL database adapter for the Python programming
language (just like pygresql and popy.) It was written from scratch with
the aim of being very small and fast, and stable as a rock. The main
advantages of psycopg are that it supports the full Python DBAPI-2.0 and
being thread safe at level 2.

%package doc
Summary: Documentation for psycopg python PostgreSQL database adapter
Group: Applications/Databases

%description doc
Documenation and example files for the psycopg python PostgreSQL
database adapter.

%prep
%setup

%build
if [ -x /usr/bin/pg_config ]; then
	POSTGRESQLINC=$(/usr/bin/pg_config --includedir-server)
	POSTGRESQLLIB=$(/usr/bin/pg_config --libdir)
else
	POSTGRESQLINC=/usr/include/pgsql
	POSTGRESQLLIB=/usr/lib
fi
%configure --with-python=%{_prefix}/bin/python%{python} \
      --with-mxdatetime-includes=%{_prefix}/lib/python%{python}/site-packages/mx/DateTime/mxDateTime  \
      --with-postgres-includes=$POSTGRESQLINC \
      --with-postgres-libs=$POSTGRESQLLIB
make

%install
mkdir -p $RPM_BUILD_ROOT/usr/lib/python%{python}/site-packages
install -m 555 psycopgmodule.so $RPM_BUILD_ROOT/usr/lib/python%{python}/site-packages

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
/usr/lib/python%{python}/site-packages/*.so

%files doc
%defattr(-,root,root)
%doc AUTHORS  COPYING  CREDITS  FAQ  INSTALL  NEWS  README  RELEASE-1.0  SUCCESS  TODO doc

