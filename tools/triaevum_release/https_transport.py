"""Verified HTTPS for both source tools and portable frozen Forge bundles."""

import ssl
import sys


def download_ssl_context() -> ssl.SSLContext:
    context = ssl.create_default_context()
    if getattr(sys, "frozen", False):
        import certifi
        # The bundled OpenSSL's build-time CA path may not exist on the user's
        # distribution. Supplement OS trust with the shipped Mozilla roots.
        context.load_verify_locations(cafile=certifi.where())
    return context
