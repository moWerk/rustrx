# the Rust core is prebuilt and bundled; rpm must neither provide nor
# require its soname
%define __provides_exclude_from ^%{_datadir}/.*$
%define __requires_exclude ^(librustrx.*|libsailfishsecrets.*)$
%define _binary_payload w6.xzdio

Name:       harbour-rustrx
Summary:    Rustrx, a Matrix client
Version:    0.4.6
Release:    1
License:    GPLv3+
URL:        https://github.com/moWerk/rustrx
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   libsailfishapp-launcher
Requires:   sailfish-components-pickers-qt5
Requires:   nemo-qml-plugin-notifications-qt5
Requires:   qt5-qtdeclarative-import-multimedia
Requires:   qt5-qtdeclarative-import-sensors
Requires:   nemo-qml-plugin-configuration-qt5
Requires:   sailfish-components-webview-qt5
Requires:   sailfishshare-components
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  desktop-file-utils
BuildRequires:  qt5-qttools-linguist

%description
Rustrx is a native Matrix client for Sailfish OS: a Silica interface on top
of matrix-rust-sdk, the library behind Element X. End-to-end encryption,
key backup and device verification come from the SDK.

%if 0%{?_chum}
Title: Rustrx
Type: desktop-application
DeveloperName: moWerk
Categories:
 - Network
 - InstantMessaging
 - Chat
Custom:
  Repo: https://github.com/moWerk/rustrx
PackageIcon: https://raw.githubusercontent.com/moWerk/rustrx/main/icons/172x172/harbour-rustrx.png
Links:
  Homepage: https://github.com/moWerk/rustrx
  Bugtracker: https://github.com/moWerk/rustrx/issues
%endif

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 VERSION=%{version}
%make_build
# the sailfish-secrets plugin (loaded at run time; see secrets/secrets.pro)
mkdir -p build-secrets && cd build-secrets && %qmake5 ../secrets/secrets.pro && %make_build && cd ..

%install
%qmake5_install
cd build-secrets && %qmake5_install && cd ..
# the call page once more, on the add-on Gecko 153 WebView (OpenRepos,
# sailfish-components-webview-qt5-next153): used only where that is
# installed, for the calls Gecko 115 can not do (encrypted ones)
sed -e 's/^import Sailfish\.WebView 1\.0$/import SailfishNext153.WebView 1.0/' \
    -e 's/^import Sailfish\.WebEngine 1\.0$/import SailfishNext153.WebEngine 1.0/' \
    %{buildroot}%{_datadir}/%{name}/qml/pages/CallPage.qml \
    > %{buildroot}%{_datadir}/%{name}/qml/pages/CallPageNext153.qml
grep -q '^import SailfishNext153.WebView 1.0$' %{buildroot}%{_datadir}/%{name}/qml/pages/CallPageNext153.qml
# the background unit is wanted by the session; it checks the app's flag file itself
mkdir -p %{buildroot}%{_userunitdir}/post-user-session.target.wants
ln -s ../harbour-rustrx.service %{buildroot}%{_userunitdir}/post-user-session.target.wants/harbour-rustrx.service
desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/dbus-1/services/net.mowerk.harbour_rustrx.service
%{_datadir}/dbus-1/services/org.unifiedpush.Connector.rustrx.service
%{_userunitdir}/harbour-rustrx.service
%{_userunitdir}/harbour-rustrx-open.service
%{_userunitdir}/post-user-session.target.wants/harbour-rustrx.service
%{_datadir}/icons/hicolor/*/apps/%{name}.png
