"""mTLS configuration and explicit certificate-to-routing-ID authorization."""
import json
import re
import ssl


def load_devices(path):
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate TLS authorization fingerprint")
            result[key] = value
        return result
    with open(path, encoding="utf-8") as stream:
        devices = json.load(stream, object_pairs_hook=unique_pairs)
    if not isinstance(devices, dict) or not devices:
        raise ValueError("TLS authorization must be a nonempty fingerprint-to-ID object")
    for fingerprint, device in devices.items():
        if (not re.fullmatch(r"[0-9a-f]{64}", fingerprint) or type(device) is not int or
                not 0 < device < 0xffffffff):
            raise ValueError("invalid SHA-256 fingerprint or device ID")
    return devices


def server_context(cert, key, ca):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(cafile=ca)
    context.load_cert_chain(certfile=cert, keyfile=key, password=lambda: b"")
    context.options |= getattr(ssl, "OP_NO_RENEGOTIATION", 0)
    # No resumable identities surviving an authorization-file change/restart.
    context.options |= ssl.OP_NO_TICKET
    context.num_tickets = 0
    return context
