import ssl
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch, Mock

import https_transport


class HttpsTransportTests(unittest.TestCase):
    def test_source_preserves_verified_system_tls(self):
        with patch.object(sys, "frozen", False, create=True):
            context = https_transport.download_ssl_context()
        self.assertEqual(context.verify_mode, ssl.CERT_REQUIRED)
        self.assertTrue(context.check_hostname)

    def test_frozen_adds_bundled_roots_without_disabling_system_trust(self):
        context = Mock()
        with patch.object(sys, "frozen", True, create=True), \
             patch.dict(sys.modules, {"certifi": SimpleNamespace(where=lambda: "/bundle/cacert.pem")}), \
             patch.object(https_transport.ssl, "create_default_context", return_value=context) as create:
            self.assertIs(https_transport.download_ssl_context(), context)
        create.assert_called_once_with()
        context.load_verify_locations.assert_called_once_with(cafile="/bundle/cacert.pem")
