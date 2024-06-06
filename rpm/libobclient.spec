Name: %NAME
Version: %VERSION
Release: %(echo %RELEASE)
License: LGPL
Group: applications/database
buildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Autoreq: no
Prefix: /u01/obclient
Summary: Oracle 5.6 and some patches from Oceanbase

%description
LibObClient is a driver used to connect applications developed in C to OceanBase Database Server.

%define MYSQL_USER root
%define MYSQL_GROUP root
%define __os_install_post %{nil}
#%define base_dir /u01/mysql
%define file_dir /app/mariadb


%prep
cd $OLDPWD/../

#%setup -q

%build
cd $OLDPWD/../
./build.sh --prefix %{prefix} --version %{version}

%install
cd $OLDPWD/../
make DESTDIR=$RPM_BUILD_ROOT install
find $RPM_BUILD_ROOT -name '.git' -type d -print0|xargs -0 rm -rf
for dir in `ls $RPM_BUILD_ROOT%{file_dir} | grep -v "bin\|share\|include\|lib"`
do
        rm -rf $RPM_BUILD_ROOT%{file_dir}/${dir}
done
mkdir -p $RPM_BUILD_ROOT%{prefix}
mv $RPM_BUILD_ROOT%{file_dir}/* $RPM_BUILD_ROOT%{prefix}
#mv $RPM_BUILD_ROOT%{prefix}/include/mariadb $RPM_BUILD_ROOT%{prefix}/include/mariadb_bak
#mv $RPM_BUILD_ROOT%{prefix}/include/mariadb_bak/* $RPM_BUILD_ROOT%{prefix}/include/
#rm -rf $RPM_BUILD_ROOT%{prefix}/include/mariadb_bak
#mv $RPM_BUILD_ROOT%{prefix}/lib/mariadb/* $RPM_BUILD_ROOT%{prefix}/lib/
#rm -rf $RPM_BUILD_ROOT%{prefix}/lib/mariadb

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-, %{MYSQL_USER}, %{MYSQL_GROUP})
%attr(755, %{MYSQL_USER}, %{MYSQL_GROUP}) %{prefix}/*
%dir %attr(755,  %{MYSQL_USER}, %{MYSQL_GROUP}) %{prefix}

%pre

%post
#if [ -d %{base_dir} ]; then
#    cp -rf %{prefix}/* %{base_dir}
#else
#    cp -rf %{prefix} %{base_dir}
#fi

%preun

%changelog

