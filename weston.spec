%global apiver 10

Name:           weston
Version:        %apiver
Release:        r0
Summary:        Reference compositor for Wayland

License:        MIT
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  glib2-devel
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  pam-devel
# ninja-build is a dependency from meson
BuildRequires:  meson
BuildRequires:  pkgconfig(cairo)
BuildRequires:  pkgconfig(cairo) >= 1.10.0
BuildRequires:  pkgconfig(cairo-xcb)
BuildRequires:  pkgconfig(dbus-1) >= 1.6
BuildRequires:  pkgconfig(lcms2)
BuildRequires:  pkgconfig(libdrm) >= 2.4.30
BuildRequires:  pkgconfig(libevdev)
BuildRequires:  pkgconfig(libinput) >= 0.8.0
BuildRequires:  pkgconfig(libpng)
BuildRequires:  pkgconfig(libsystemd) >= 209
BuildRequires:  pkgconfig(libudev) >= 136
BuildRequires:  pkgconfig(libwebp)
BuildRequires:  pkgconfig(libxml-2.0) >= 2.6
BuildRequires:  pkgconfig(mtdev) >= 1.1.0
BuildRequires:  pkgconfig(pangocairo)
BuildRequires:  pkgconfig(pixman-1) >= 0.25.2
BuildRequires:  pkgconfig(wayland-client) >= 1.12.0
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(wayland-protocols) >= 1.24
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  poppler-devel
BuildRequires:  poppler-glib-devel
BuildRequires:  gstreamer1-devel
BuildRequires:  libgbm-dev
BuildRequires:  weston-sdm-extension-headers
BuildRequires: systemd systemd-rpm-macros

Requires:       %{name}-libs%{?_isa} = %{version}-%{release}
Requires:       libcutils systemd libevdev mtdev libgudev libwacom-data libwacom libinput libwayland-cursor libwayland-egl
Requires:       pkgconfig(libdrm) = 2.4.110

%{?systemd_requires}
BuildRequires: adreno200-binaries
BuildRequires: owfds-dev

%description
Weston is the reference wayland compositor that can run on KMS, under X11
or under another compositor.

%global debug_package %{nil}

%package        libs
Summary:        Weston compositor libraries

%description    libs
This package contains Weston compositor libraries.

%package        demo
Summary:        Weston demo program files

%description    demo
This package contains Weston demo program files.

%package        devel
Summary:        Common headers for weston
License:        MIT
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       %{name}-libs%{?_isa} = %{version}-%{release}

%description    devel
Common headers for weston

%prep
#%setup -q
%autosetup -n weston

%build
# ninja injects -Wl,--no-undefined, which intereferes with LTO, so undo
# the setting.  Thanks to the SuSE folks for the workaround.
export LDFLAGS="%{?build_ldflags} -Wl,-z,undefs"
export CPPFLAGS="-I/usr/include/gbm"
%meson -Dremoting=false -Dbackend-drm-screencast-vaapi=false  -Dcolor-management-colord=false -Dbackend-x11=false -Dbackend-rdp=false -Dpipewire=false -Dbackend-rdp=false -Dxwayland=false -Dsystemd=true -Dlauncher-logind=true -Ddeprecated-wl-shell=true
%meson_build


%install
%meson_install

mkdir -p %{buildroot}%{_libdir}/drm-back
mv %{buildroot}%{_libdir}/libweston-%{apiver}/drm-backend.so %{buildroot}%{_libdir}/drm-back

mkdir -p %{buildroot}%{_sysconfdir}/pam.d
install -DpZm 0644 weston-autologin %{buildroot}%{_sysconfdir}/pam.d/weston-autologin
mkdir -p %{buildroot}%{_unitdir}
install -DpZm 0644 weston.service %{buildroot}%{_unitdir}/weston.service
mkdir -p %{buildroot}%{_sysconfdir}/xdg/weston
install -DpZm 0644 weston.ini %{buildroot}%{_sysconfdir}/xdg/weston/weston.ini


%post
systemctl enable weston.service

%preun
%systemd_preun weston.service

%postun
%systemd_postun_with_restart weston.service


%check
# may be standalone tests can be done
#%%meson_test

%files
%license COPYING
%doc README.md
%{_bindir}/weston
%{_bindir}/weston-debug
%{_bindir}/weston-info
%{_bindir}/weston-screenshooter
%{_bindir}/weston-terminal
%{_bindir}/wcap-decode
%dir %{_libdir}/weston
#%{_libdir}/weston/cms-colord.so
%{_libdir}/weston/cms-static.so
%{_libdir}/weston/desktop-shell.so
%{_libdir}/weston/fullscreen-shell.so
%{_libdir}/weston/hmi-controller.so
%{_libdir}/weston/ivi-shell.so
%{_libdir}/weston/screen-share.so
%{_libdir}/weston/systemd-notify.so
%{_libdir}/weston/kiosk-shell.so
%{_libdir}/weston/libexec_weston.so*
#%{_libdir}/libbacklight.so
#%{_libdir}/liblibinput-backend.so
#%{_libdir}/libsession-helper.so
%{_libexecdir}/weston-*
%{_mandir}/man1/*.1*
%{_mandir}/man5/*.5*
%{_mandir}/man7/*.7*
%dir %{_datadir}/weston
%{_datadir}/weston/*.png
%{_datadir}/weston/wayland.svg
%{_datadir}/wayland-sessions/weston.desktop
%{_unitdir}/weston.service
%{_sysconfdir}/pam.d/weston-autologin
%{_sysconfdir}/xdg/weston/weston.ini

%files libs
%license COPYING
%dir %{_libdir}/libweston-%{apiver}
%{_libdir}/libweston-%{apiver}/color-lcms.so
%{_libdir}/libbacklight.so
%{_libdir}/liblibinput-backend.so
%{_libdir}/libsession-helper.so

#Installing drm-backend.so in a different directory , needed for yocto, so have to package it here to avoid RPM errors*/
%{_libdir}/drm-back/drm-backend.so
%{_libdir}/libweston-%{apiver}/gl-renderer.so
%{_libdir}/libweston-%{apiver}/headless-backend.so
#%{_libdir}/libweston-%{apiver}/pipewire-plugin.so
#%{_libdir}/libweston-%{apiver}/remoting-plugin.so
#%{_libdir}/libweston-%{apiver}/rdp-backend.so
#%{_libdir}/libweston-%{apiver}/wayland-backend.so
#%{_libdir}/libweston-%{apiver}/x11-backend.so
#%{_libdir}/libweston-%{apiver}/xwayland.so
%{_libdir}/libweston-%{apiver}.so.0*
%{_libdir}/libweston-desktop-%{apiver}.so.0*

%files demo
%license COPYING
%{_bindir}/weston-calibrator
%{_bindir}/weston-clickdot
%{_bindir}/weston-cliptest
%{_bindir}/weston-confine
%{_bindir}/weston-dnd
%{_bindir}/weston-editor
%{_bindir}/weston-eventdemo
%{_bindir}/weston-flower
%{_bindir}/weston-fullscreen
%{_bindir}/weston-image
%{_bindir}/weston-multi-resource
%{_bindir}/weston-presentation-shm
%{_bindir}/weston-resizor
%{_bindir}/weston-scaler
%{_bindir}/weston-simple-damage
%{_bindir}/weston-content_protection
%{_bindir}/weston-simple-dmabuf-egl
%{_bindir}/weston-simple-dmabuf-feedback
%{_bindir}/weston-simple-dmabuf-v4l
%{_bindir}/weston-simple-egl
%{_bindir}/weston-simple-shm
%{_bindir}/weston-simple-touch
%{_bindir}/weston-smoke
%{_bindir}/weston-stacking
%{_bindir}/weston-subsurfaces
%{_bindir}/weston-touch-calibrator
%{_bindir}/weston-transformed

%files devel
%{_includedir}/libweston-%{apiver}/
%{_includedir}/weston/
%{_includedir}/weston-shared/
%{_libdir}/pkgconfig/libweston-%{apiver}.pc
%{_libdir}/pkgconfig/libweston-desktop-%{apiver}.pc
%{_libdir}/pkgconfig/weston.pc
%{_libdir}/libweston-%{apiver}.so
%{_libdir}/libweston-desktop-%{apiver}.so
%{_datadir}/pkgconfig/libweston-%{apiver}-protocols.pc
%{_datadir}/libweston-%{apiver}/protocols/


