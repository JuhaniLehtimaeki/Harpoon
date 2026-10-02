Name:       harpoon
Summary:    Install and update apps from GitHub, Codeberg and other forges
Version:    0.1.0
Release:    1
# Parts of the core are ported from ObtainX (GPL-3.0).
License:    GPLv3+
URL:        https://github.com/JuhaniLehtimaeki/Harpoon
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   nemo-qml-plugin-notifications-qt5
Requires:   rpm
Requires:   PackageKit
Requires:   systemd
BuildRequires:  cmake
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils

%description
Harpoon tracks apps that publish RPM packages as release assets on code forges
(GitHub, Codeberg, Forgejo, Gitea) and installs updates straight from the source.
It also contains the command line tool harpoon-cli.

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
desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/harpoon
%{_bindir}/harpoon-cli
%{_datadir}/harpoon
%{_datadir}/applications/harpoon.desktop
%{_datadir}/icons/hicolor/*/apps/harpoon.png
%{_datadir}/mapplauncherd/privileges.d/harpoon
%{_datadir}/dbus-1/services/io.github.juhanilehtimaeki.harpoon.service
%{_prefix}/lib/systemd/user/harpoon-check.service
%{_prefix}/lib/systemd/user/harpoon-check.timer
