# the Rust core is prebuilt and bundled; rpm must neither provide nor
# require its soname
%define __provides_exclude_from ^%{_datadir}/.*$
%define __requires_exclude ^(librustrx.*|libsailfishsecrets.*)$
%define _binary_payload w6.xzdio

Name:       harbour-rustrx
Summary:    Rustrx, a Matrix client
Version:    0.5.0
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
# calls: webrtcbin, DTLS and SRTP (bad), ICE (libnice), Opus and RTP (base, good)
Requires:   gstreamer1.0-plugins-bad
Requires:   gstreamer1.0-plugins-good
Requires:   gstreamer1.0-plugins-base
Requires:   libnice
# echo cancelling on calls
Requires:   speexdsp
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-sdp-1.0)
BuildRequires:  pkgconfig(gstreamer-webrtc-1.0)
BuildRequires:  pkgconfig(gstreamer-app-1.0)
BuildRequires:  pkgconfig(speexdsp)
BuildRequires:  pkgconfig(libpulse)
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

# The audio policy reads its rules when PulseAudio starts: restarted when
# the call rule first arrives or its content changed, never on an update
# that leaves it as it was (a restart cuts whatever plays for a moment).
# %pre keeps the installed rule in /run, %post compares it with the new.
%pre
rule=/etc/pulse/xpolicy.conf.d/harbour-rustrx-xpolicy.conf
rm -f /run/harbour-rustrx-xpolicy-new /run/harbour-rustrx-xpolicy-old || :
if [ -e $rule ]; then
    cp -p $rule /run/harbour-rustrx-xpolicy-old || touch /run/harbour-rustrx-xpolicy-new || :
else
    touch /run/harbour-rustrx-xpolicy-new || :
fi

%post
rule=/etc/pulse/xpolicy.conf.d/harbour-rustrx-xpolicy.conf
restart=
[ -e /run/harbour-rustrx-xpolicy-new ] && restart=1
[ -e /run/harbour-rustrx-xpolicy-old ] && ! cmp -s /run/harbour-rustrx-xpolicy-old $rule && restart=1
rm -f /run/harbour-rustrx-xpolicy-new /run/harbour-rustrx-xpolicy-old || :
if [ -n "$restart" ]; then
    systemctl-user try-restart pulseaudio.service >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/dbus-1/services/net.mowerk.harbour_rustrx.service
%{_datadir}/dbus-1/services/org.unifiedpush.Connector.rustrx.service
%{_datadir}/lipstick/notificationcategories/x-harbour-rustrx.*.conf
%config %{_sysconfdir}/pulse/xpolicy.conf.d/harbour-rustrx-xpolicy.conf
%{_userunitdir}/harbour-rustrx.service
%{_userunitdir}/harbour-rustrx-open.service
%{_userunitdir}/post-user-session.target.wants/harbour-rustrx.service
%{_datadir}/icons/hicolor/*/apps/%{name}.png
