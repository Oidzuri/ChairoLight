# Third-party components

ChairoLight release packages contain dynamically linked third-party components. Their copyrights remain with their respective owners.

## Qt 5

Qt Core, GUI, Widgets, Network, SerialPort and Windows platform plugins are provided by The Qt Company and Qt contributors. The distributed MinGW build is used under the GNU Lesser General Public License v3 (with applicable Qt exceptions) or another license available to the recipient.

Source and license information: https://www.qt.io/licensing/open-source-lgpl-obligations

The portable packaging script copies the corresponding Qt license texts from the build toolchain into `licenses/Qt`.

## GCC / MinGW runtime

The archive includes MinGW-w64/GCC runtime libraries required by the executable. The packaging script includes the runtime license and exception texts from the build toolchain in `licenses/GCC` and `licenses/WinPthreads`.

## Project dependencies

Additional source dependencies retain the notices and license headers stored in their source directories. Nothing in this file replaces the complete license texts included in the release archive.
