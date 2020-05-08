# conduitd
This software is a prototype not suitable for production.

The purpose of this software is to implement the 'Conduit' concept in the VM side (see wiki https://github.com/720k/virt-viewer/wiki ). 

ConduitD windows service listen on tcp localhost:60900 and redirect data to system port \\.\global\io.bplayer.data.0
technically it should listen on \\.\pipe\io.bplayer.data.0 but GLib doesn't care about 'windows local socket' at all, it will be fixed soon.

Context: Inside Windows virtual machine connected with Spice protocol to KVM/QEMU ( https://libvirt.org/drvqemu.html ) .

Original source: spice-webdavd.c ( https://gitlab.gnome.org/GNOME/phodav )

Building:
-  project built by QtCreator, QT framework https://www.qt.io/

Dependencies:
-  GLib        ( installed with VCPKG https://github.com/Microsoft/vcpkg )
-  WindowsKit  ( instaleld by Visual Studio Community https://visualstudio.microsoft.com/it/vs/community/ )

Tests: tested w/o installation, like any other executable. Either you executed with Admin rights or you can grant 'Users' group full access to system port with WinObjEx64 ( https://github.com/hfiref0x/WinObjEx64 ).
  
