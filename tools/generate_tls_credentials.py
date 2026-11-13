#!/usr/bin/env python3
"""Create a NEW private, off-repository laboratory mTLS PKI. Never deploy/flash."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]


def generate(output, dns, ips, days=365):
    output = Path(output).expanduser().resolve()
    if output == REPO or REPO in output.parents:
        raise ValueError("TLS credentials must be outside the repository")
    if not dns and not ips:
        raise ValueError("at least one server DNS name or IP SAN is required")
    if not 1 <= days <= 3650:
        raise ValueError("invalid certificate lifetime")
    for name in dns:
        if len(name) > 253 or not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?", name):
            raise ValueError("invalid DNS SAN")
    ips = [str(ipaddress.ip_address(ip)) for ip in ips]
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    os.chmod(output, 0o700)

    def run(*args):
        result = subprocess.run(["openssl", *args], capture_output=True)
        if result.returncode:
            raise RuntimeError("OpenSSL credential generation failed")

    def key(name):
        run("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
            "-out", str(output / f"{name}.key.pem"))
        os.chmod(output / f"{name}.key.pem", 0o600)

    key("ca")
    config = output / "ca.cnf"
    config.write_text("[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n"
                      "[dn]\nCN=Falafel laboratory CA\n[ext]\n"
                      "basicConstraints=critical,CA:TRUE,pathlen:0\n"
                      "keyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\n")
    run("req", "-new", "-x509", "-sha256", "-days", str(days), "-key",
        str(output / "ca.key.pem"), "-config", str(config), "-out", str(output / "ca.pem"))
    sans = ["DNS:" + name for name in dns] + ["IP:" + ip for ip in ips]
    devices = {}
    for name, purpose, device in (("server", "serverAuth", None),
                                   ("c6", "clientAuth", 1), ("simulator", "clientAuth", 2)):
        key(name)
        csr = output / f"{name}.csr"
        run("req", "-new", "-sha256", "-key", str(output / f"{name}.key.pem"),
            "-subj", f"/CN=Falafel {name}", "-out", str(csr))
        extensions = output / f"{name}.ext"
        extensions.write_text("basicConstraints=critical,CA:FALSE\n"
            "keyUsage=critical,digitalSignature\n" + f"extendedKeyUsage={purpose}\n"
            "subjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n" +
            ("subjectAltName=" + ",".join(sans) + "\n" if name == "server" else ""))
        cert = output / f"{name}.pem"
        run("x509", "-req", "-sha256", "-days", str(days), "-in", str(csr),
            "-CA", str(output / "ca.pem"), "-CAkey", str(output / "ca.key.pem"),
            "-set_serial", str(int.from_bytes(os.urandom(16), "big") or 1),
            "-extfile", str(extensions), "-out", str(cert))
        if device:
            devices[hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()] = device
        csr.unlink()
        extensions.unlink()
    config.unlink()
    (output / "devices.json").write_text(json.dumps(devices, indent=2) + "\n")
    for path in output.iterdir():
        os.chmod(path, 0o600)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--server-dns", action="append", default=[])
    parser.add_argument("--server-ip", action="append", default=[])
    parser.add_argument("--days", type=int, default=365)
    args = parser.parse_args()
    os.umask(0o077)
    output = generate(args.output_dir, args.server_dns, args.server_ip, args.days)
    print("Prepared private TLS credentials:", output)
    print("Keep ca.key.pem offline; distribute only each endpoint's own key/cert and ca.pem.")
    print("No E2EE identity, NVS, running service or device was modified.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, RuntimeError):
        print("TLS provisioning failed; use new private external paths and check OpenSSL/SANs.", file=sys.stderr)
        sys.exit(1)
