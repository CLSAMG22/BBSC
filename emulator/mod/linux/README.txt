BBSC - Bloodborne Seamless Co-op, for BBport on Linux (test build)
==================================================================

Needs: BBport for Linux (Bloodborne-bbport-x86_64.AppImage) set up with your own Bloodborne
CUSA03173 01.09. Back up your saves before playing.

Install
  1. Extract this folder anywhere.
  2. In a terminal, in this folder:   ./install.sh
     (it finds the AppImage in ~/Applications, ~/Downloads, ~/Games or ~; otherwise give its
     path: ./install.sh /path/to/Bloodborne-bbport-x86_64.AppImage)
  3. Start "Bloodborne (BBSC)" from the application menu. It is BBport's usual launcher; set up
     the Online page as usual and turn on "Seamless co-op (BBSC)".
BBport started the usual way stays without BBSC. Uninstall: ./install.sh --uninstall

Playing: the host rings the Hunt Bell, the guests the Kin Bell.

Logs (for bug reports, with the launcher's log: Log page > Export):
  ~/.local/share/bbport/bbsc/logs/

Not on Linux yet: the keyboard/mouse camera (F1) and the F8 unstuck key. The controller camera
(L3 + R3) works; it also opens BBport's menu.

Source and license: https://github.com/Classmg22/BBSC (GPL-2.0-or-later, see LICENSE.txt and
NOTICE.txt).
