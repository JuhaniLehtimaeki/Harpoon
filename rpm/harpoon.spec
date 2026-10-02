Name:       harpoon
Summary:    Install and update apps from GitHub, Codeberg and other forges
Version:    0.1.0
Release:    1
# Provisional: parts of the core are ported from ObtainX, which is GPL-3.0.
# The project licence is not decided yet; see README.
License:    GPLv3
URL:        https://github.com/JuhaniLehtimaeki/Harpoon
Source0:    %{name}-%{version}.tar.bz2
Requires:   rpm
Requires:   PackageKit
BuildRequires:  cmake
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5DBus)

%description
Harpoon tracks apps that publish RPM packages as release assets on code forges
(GitHub, Codeberg, Forgejo, Gitea) and installs updates straight from the source.
This package currently contains the command line tool harpoon-cli.

%build
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DHARPOON_BUILD_TESTS=OFF \
    -DHARPOON_BUILD_TOOLS=OFF
make %{?_smp_mflags}

%install
cd build
make install DESTDIR=%{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/harpoon-cli
