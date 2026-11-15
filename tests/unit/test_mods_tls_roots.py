"""Build 83: setup/mods.py trusts the shipped ISRG roots (config/mods/isrg-roots.pem) after a certificate failure.

Players whose Windows root store lacked ISRG Root X1 never got community packs: Python's ssl reads the store as it
is ("certificate verify failed: certificate has expired", the old DST Root CA X3 path) while WinHTTP fetches a
missing root itself. Here a local HTTPS server's certificate chains only to a throw-away test root, which plays the
part of the shipped roots: the system store refuses it, the fallback accepts it, the hostname is still checked."""
import datetime as dt
import hashlib
import http.server
import importlib.util
from pathlib import Path
import ssl
import threading
import urllib.error

import pytest

pytest.importorskip('cryptography')
from cryptography import x509  # noqa: E402
from cryptography.hazmat.primitives import hashes, serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402
from cryptography.x509.oid import NameOID  # noqa: E402
import ipaddress  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mods_tls_under_test', ROOT / 'setup/mods.py')
mods = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mods)

BODY = b'{"catalogue": "test"}'


def name(cn):
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def make_chain(folder):
    now = dt.datetime.now(dt.timezone.utc)
    root_key = ec.generate_private_key(ec.SECP256R1())
    root = (x509.CertificateBuilder().subject_name(name('FFNY test root')).issuer_name(name('FFNY test root'))
            .public_key(root_key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - dt.timedelta(days=1)).not_valid_after(now + dt.timedelta(days=30))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .add_extension(x509.KeyUsage(digital_signature=True, key_cert_sign=True, crl_sign=True,
                                         content_commitment=False, key_encipherment=False, data_encipherment=False,
                                         key_agreement=False, encipher_only=False, decipher_only=False), critical=True)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(root_key.public_key()), critical=False)
            .sign(root_key, hashes.SHA256()))
    leaf_key = ec.generate_private_key(ec.SECP256R1())
    leaf = (x509.CertificateBuilder().subject_name(name('localhost')).issuer_name(root.subject)
            .public_key(leaf_key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - dt.timedelta(days=1)).not_valid_after(now + dt.timedelta(days=30))
            .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost')]), critical=False)
            .add_extension(x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), critical=False)
            .sign(root_key, hashes.SHA256()))
    pem = lambda c: c.public_bytes(serialization.Encoding.PEM)
    (folder / 'roots.pem').write_bytes(b'# a test root, in the place of ISRG Root X1\n' + pem(root))
    (folder / 'leaf.pem').write_bytes(pem(leaf))
    (folder / 'leaf.key').write_bytes(leaf_key.private_bytes(serialization.Encoding.PEM,
                                                             serialization.PrivateFormat.PKCS8,
                                                             serialization.NoEncryption()))
    return folder / 'roots.pem'


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Length', str(len(BODY)))
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, *args):
        pass


@pytest.fixture
def server(tmp_path, monkeypatch):
    roots = make_chain(tmp_path)
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(tmp_path / 'leaf.pem', tmp_path / 'leaf.key')
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    monkeypatch.setattr(mods, '_roots_only', False)
    yield httpd.server_address[1], roots
    httpd.shutdown()
    httpd.server_close()


def test_the_system_store_refuses_the_test_root(server, monkeypatch, tmp_path):
    port, _ = server
    monkeypatch.setattr(mods, 'ROOTS_PEM', tmp_path / 'missing.pem')     # no shipped roots: the old behaviour
    with pytest.raises(urllib.error.URLError) as caught:
        mods.fetch_small('https://localhost:%d/updates/content/index-v3.json' % port, 1000)
    assert mods.cert_failure(caught.value)
    kind, text = mods.plain_reason(['Catalogue: ' + str(caught.value)])
    assert kind == 'clock' and 'date and time' in text


def test_the_shipped_roots_are_tried_after_a_certificate_failure(server, monkeypatch):
    port, roots = server
    monkeypatch.setattr(mods, 'ROOTS_PEM', roots)
    assert mods.fetch_small('https://localhost:%d/updates/content/index-v3.json' % port, 1000) == BODY
    assert mods._roots_only     # later calls go straight to them
    assert mods.fetch_small('https://localhost:%d/updates/content/index-v3.json.sig' % port, 1000) == BODY


def test_the_hostname_is_still_checked_with_the_shipped_roots(server, monkeypatch):
    port, roots = server
    monkeypatch.setattr(mods, 'ROOTS_PEM', roots)
    with pytest.raises((urllib.error.URLError, ssl.SSLError)):
        mods.fetch_small('https://127.0.0.1:%d/updates/content/index-v3.json' % port, 1000)   # the cert names localhost


def test_the_shipped_pem_holds_exactly_isrg_root_x1_and_x2():
    text = (ROOT / 'config/mods/isrg-roots.pem').read_text(encoding='ascii')
    blocks = text.split('-----BEGIN CERTIFICATE-----')[1:]
    prints = {hashlib.sha256(ssl.PEM_cert_to_DER_cert('-----BEGIN CERTIFICATE-----' + b.split('-----END CERTIFICATE-----')[0]
                                                       + '-----END CERTIFICATE-----\n')).hexdigest() for b in blocks}
    assert prints == {'96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6',     # ISRG Root X1
                      '69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470'}     # ISRG Root X2
    assert mods.ROOTS_PEM == ROOT / 'config/mods/isrg-roots.pem'
    package = (ROOT / 'scripts/package-setup.py').read_text(encoding='utf-8')
    assert "'config/mods/isrg-roots.pem'" in package      # in the setup payload, next to the catalogue keys


def test_every_https_call_goes_through_open_url():
    source = (ROOT / 'setup/mods.py').read_text(encoding='utf-8')
    assert source.count('build_opener(') == 1 and 'urlopen(' not in source.replace('urlopen error', '')
    assert source.count('open_url(') >= 3       # its definition, fetch_small and the pack download
