Summary: qemu-dm device model
Name: xen-device-model
Version: @IOEMU_VERSION@
Release: @IOEMU_RELEASE@
License: GPL
Group: System/Hypervisor
Source0: xen-device-model-%{version}.tar.bz2
Patch0: xen-device-model-development.patch
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
BuildRequires: SDL-devel, zlib-devel, xen-devel, ncurses-devel, pciutils-devel

%description
This package contains qemu-dm, the Xen device model.
%prep
%setup -q
%patch0 -p1

%build
./xen-setup --disable-opengl --disable-vnc-tls --disable-blobs
%{__make}

%install
rm -rf $RPM_BUILD_ROOT
%{__make} install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/var/xen/qemu
rm -f $RPM_BUILD_ROOT/usr/bin/qemu-img-xen
rm -f $RPM_BUILD_ROOT/etc/xen/qemu-ifup

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%doc
%{_libdir}/xen/bin/qemu-dm
%{_datadir}/xen/qemu/keymaps
%dir /var/xen/qemu

%changelog
