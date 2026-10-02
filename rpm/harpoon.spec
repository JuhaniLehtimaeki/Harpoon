Name:       harpoon
# The version must match a git tag (see docs/packaging.md).
Summary:    Install and update apps from GitHub, Codeberg and other forges
Version:    0.1.1
Release:    1
# Parts of the core are ported from ObtainX (GPL-3.0).
# zxing-cpp (3rdparty/, Apache-2.0) is linked into the app.
License:    GPLv3+ and ASL 2.0
URL:        https://github.com/JuhaniLehtimaeki/Harpoon
Source0:    %{name}-%{version}.tar.bz2
Group:      Software Management/Package Manager
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   nemo-qml-plugin-notifications-qt5
# QtMultimedia QML import for the QR code scanner's camera.
Requires:   qt5-qtdeclarative-import-multimedia
Requires:   rpm
Requires:   PackageKit
Requires:   systemd
# invoker starts the background job with the privileged group.
Requires:   mapplauncherd
BuildRequires:  cmake
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(zlib)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils

%description
Harpoon tracks apps that publish RPM packages as release assets on code forges
(GitHub, Codeberg, Forgejo, Gitea, GitLab and others) and installs updates
straight from the source. It also contains the command line tool harpoon-cli.

# Metadata for SailfishOS:Chum, see
# https://github.com/sailfishos-chum/main/blob/main/Metadata.md
%if 0%{?_chum}
Title: Harpoon
Type: desktop-application
DeveloperName: Juhani Lehtimäki
Categories:
 - System
 - PackageManager
 - Utility
Custom:
  Repo: %{url}
PackageIcon: %{url}/raw/main/gui/icons/harpoon.svg
Links:
  Homepage: %{url}
  Help: %{url}/blob/main/README.md
  Bugtracker: %{url}/issues
%endif

%prep
# OBS (Chum) unpacks the tarball made by tar_git; sfdk builds in place.
%if %(test -e %{SOURCE0} && echo 1 || echo 0)
%setup -q -n %{name}-%{version}
%endif

%build
cp 3rdparty/zxing-cpp/LICENSE LICENSE.zxing-cpp
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
# install/strip: the SailfishOS build does not strip the binaries itself.
make install/strip DESTDIR=%{buildroot}
desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%license LICENSE LICENSE.zxing-cpp
%{_bindir}/harpoon
%{_bindir}/harpoon-cli
%{_bindir}/harpoon-autoupdate
%{_datadir}/harpoon
%{_datadir}/applications/harpoon.desktop
%{_datadir}/icons/hicolor/*/apps/harpoon.png
%{_datadir}/mapplauncherd/privileges.d/harpoon
%{_datadir}/dbus-1/services/io.github.juhanilehtimaeki.harpoon.service
%{_prefix}/lib/systemd/user/harpoon-check.service
%{_prefix}/lib/systemd/user/harpoon-check.timer
