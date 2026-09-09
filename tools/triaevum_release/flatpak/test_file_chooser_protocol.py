"""Run with dbus-run-session -- python3 this_file.py /path/to/compiled/chooser."""

from pathlib import Path
import subprocess
import sys
import threading
import unittest

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib


HELPER = str(Path(sys.argv.pop(1)).resolve())
PORTAL = "org.freedesktop.portal.Desktop"
REQUEST = "org.freedesktop.portal.Request"
XML = """<node>
<interface name="org.freedesktop.portal.FileChooser">
 <method name="OpenFile"><arg type="s" direction="in"/><arg type="s" direction="in"/>
 <arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
</interface>
<interface name="org.freedesktop.portal.Request">
 <method name="Close"/><signal name="Response"><arg type="u"/><arg type="a{sv}"/></signal>
</interface></node>"""


class PortalProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        cls.bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "RequestName", GLib.Variant("(su)", (PORTAL, 0)),
                          GLib.VariantType.new("(u)"), Gio.DBusCallFlags.NONE, 1000, None)
        cls.info = Gio.DBusNodeInfo.new_for_xml(XML)

    def setUp(self):
        self.calls = []
        self.closed = False
        self.registered = []
        self.scenario = "selected"
        self.registered.append(self.bus.register_object("/org/freedesktop/portal/desktop",
                               self.info.interfaces[0], self.handle, None, None))

    def tearDown(self):
        for registration in self.registered:
            self.bus.unregister_object(registration)

    def handle(self, connection, sender, path, interface, method, parameters, invocation):
        if method == "Close":
            self.closed = True
            invocation.return_value(GLib.Variant("()", ()))
            return
        parent, title, options = parameters.unpack()
        self.calls.append((parent, title, options))
        path = ("/org/freedesktop/portal/desktop/request/" + sender[1:].replace(".", "_")
                + "/" + options["handle_token"])
        if self.scenario == "different_handle":
            path += "_alternate"
        self.registered.append(self.bus.register_object(path, self.info.interfaces[1], self.handle, None, None))
        if self.scenario != "timeout":
            response = 1 if self.scenario == "cancel" else 0
            uris = ["file:///run/user/1000/doc/test/Personal%20ROM%25.cci"]
            if self.scenario == "multiple":
                uris *= 2
            values = {"uris": GLib.Variant("as", uris)}
            # Deliberately send the response before the method reply, including
            # one unrelated path that a client must never accept.
            connection.emit_signal(sender, path + "_unrelated", REQUEST, "Response",
                                   GLib.Variant("(ua{sv})", (1, {})))
            connection.emit_signal(sender, path, REQUEST, "Response",
                                   GLib.Variant("(ua{sv})", (response, values)))
        invocation.return_value(GLib.Variant("(o)", (path,)))

    def invoke(self, *args):
        loop = GLib.MainLoop()
        result = []
        def worker():
            try:
                result.append(subprocess.run([HELPER, "--timeout-seconds", "1", *args],
                                             capture_output=True, text=True, timeout=6))
            except Exception as exc:
                result.append(exc)
            finally:
                GLib.idle_add(loop.quit)
        thread = threading.Thread(target=worker)
        thread.start()
        loop.run()
        thread.join()
        if isinstance(result[0], Exception):
            raise result[0]
        return result[0]

    def test_fast_response_and_filter_parent_contract(self):
        result = self.invoke("--title", "Personal ROM", "--parent", "x11:123")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "file:///run/user/1000/doc/test/Personal%20ROM%25.cci\n")
        parent, title, options = self.calls[0]
        self.assertEqual((parent, title), ("x11:123", "Personal ROM"))
        self.assertFalse(options["multiple"])
        self.assertEqual(options["filters"][0][1], [(0, "*.[3][dD][sS]"), (0, "*.[cC][cC][iI]")])

    def test_returned_handle_is_authoritative(self):
        self.scenario = "different_handle"
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_directory_selection(self):
        result = self.invoke("--directory")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.calls[0][2]["directory"])
        self.assertNotIn("filters", self.calls[0][2])

    def test_cancel(self):
        self.scenario = "cancel"
        result = self.invoke()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_multiple_files_fail_closed(self):
        self.scenario = "multiple"
        self.assertEqual(self.invoke().returncode, 1)

    def test_timeout_closes_portal_request(self):
        self.scenario = "timeout"
        self.assertEqual(self.invoke().returncode, 3)
        self.assertTrue(self.closed)


if __name__ == "__main__":
    unittest.main()
