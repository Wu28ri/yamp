# Third-Party Software Notices

The supported Arch Linux package dynamically links to the replaceable system
libraries listed below. These components are not part of the YAMP source code
and remain copyrighted by their respective authors. Open-source YAMP builds use
the license options identified here. Static builds and self-contained bundles
require a separate audit and additional source, notice, and relinking material.

## Qt 6

Modules used directly or through QML/runtime dependencies include Qt Core, GUI,
OpenGL, QML, QML Models, QML WorkerScript, Quick, Quick Controls and styles,
Quick Templates, Layouts, Dialogs, SQL/QSQLITE, DBus, and Network.

License option used by YAMP: GNU General Public License version 3 only.

- Project: https://www.qt.io/
- Source: https://code.qt.io/cgit/
- License information: https://doc.qt.io/qt-6/licensing.html

## mpv / libmpv

License option used by YAMP: GNU General Public License version 3, selected
under mpv's GPL-2.0-or-later terms. An LGPL-only custom build of mpv may have
different obligations.

- Project and source: https://github.com/mpv-player/mpv
- Copyright and license details: https://github.com/mpv-player/mpv/blob/master/Copyright

## TagLib

TagLib is available under GNU Lesser General Public License version 2.1 or the
Mozilla Public License 1.1. The supported package uses TagLib as a replaceable
shared library under LGPLv2.1 section 6(b); TagLib itself remains under its
upstream terms.

- Project and source: https://github.com/taglib/taglib

## PulseAudio libpulse

License: GNU Lesser General Public License version 2.1 or later. The supported
package uses libpulse as a replaceable shared library under LGPLv2.1 section
6(b); libpulse itself remains under its upstream terms.

- Project and source: https://gitlab.freedesktop.org/pulseaudio/pulseaudio

## ALSA libasound

License: GNU Lesser General Public License version 2.1 or later. The supported
package uses libasound as a replaceable shared library under LGPLv2.1 section
6(b); libasound itself remains under its upstream terms.

- Project and source: https://github.com/alsa-project/alsa-lib

## SQLite

YAMP accesses SQLite through Qt's QSQLITE driver. SQLite is dedicated to the
public domain.

- Project: https://sqlite.org/
- Copyright statement: https://sqlite.org/copyright.html

## zlib-compatible library

TagLib's link interface uses `libz`. The audited Arch build obtains it from
zlib-ng-compat under the zlib License; another supported system may provide the
reference zlib implementation under the same license.

- Audited provider and source: https://github.com/zlib-ng/zlib-ng
- Reference implementation: https://zlib.net/

## Last.fm

Last.fm integration uses a network API and does not include Last.fm software.
Use of that optional integration is subject to the Last.fm API Terms. Last.fm
requires its supplied “powered by AudioScrobbler” button and prior written
approval for public placements. The upstream resources page is currently
unavailable; YAMP's text button does not represent approval by Last.fm.

- Service: https://www.last.fm/
- API terms: https://www.last.fm/api/tos

## Binary distribution

Distributors of a YAMP executable must provide the exact corresponding YAMP
source. Because the supported build selects GPLv3 for Qt and GPLv3 under
libmpv's GPL-2.0-or-later terms, distributors must also ensure that the exact
corresponding sources and build information for the linked Qt and libmpv builds
are available as required by GPLv3. Merely linking them dynamically does not
remove GPL source obligations.

The AUR recipe distributes source and builds against system packages. An
AppImage, portable archive, or other self-contained bundle has a larger runtime
dependency set and is not covered by this notice without a separate audit.

The complete GPL version 3 text is in `LICENSE`, and the complete LGPL version
2.1 text is in `licenses/LGPL-2.1.txt`. A distribution bundle that includes
copies of these libraries must also include their exact upstream copyright and
license notices, corresponding sources, and build information as required by
their licenses.
