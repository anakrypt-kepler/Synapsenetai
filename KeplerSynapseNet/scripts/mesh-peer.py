#!/usr/bin/env python3
# Headless mesh peer for the desktop NET map: GET_PEERS / SYNAPSE_PEER / NODE_PROFILE
# plus a tiny JSON-RPC on 8332 (peer.announce / peer.directory / blocks.list).
# Speaks the same onion protocol as libsynapsed, not the synapsed daemon P2P stack.
# Own identity is a CC0 hoodie portrait, never the desktop operator's profile_avatar.
import base64
import ctypes
import ctypes.util
import hashlib
import io
import json
import os
import socket
import subprocess
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CONTROL_HOST = "127.0.0.1"
CONTROL_PORT = 9051
SOCKS_HOST = "127.0.0.1"
SOCKS_PORT = 9050
P2P_PORT = 8333
RPC_PORT = 8332
COOKIE_PATHS = (
    "/run/tor/control.authcookie",
    "/var/run/tor/control.authcookie",
    "/var/lib/tor/control_auth_cookie",
)
STATE_DIR = os.environ.get("MESH_STATE", "/root/synapsenet-mesh")
ALIAS = os.environ.get("MESH_ALIAS", "VPS")[:32]
# Wikimedia Commons CC0: "Cliche Hacker and Binary Code" (hooded person at a keyboard).
AVATAR_PAGE = (
    "https://commons.wikimedia.org/wiki/File:Cliche_Hacker_and_Binary_Code_(26946304530).jpg"
)
AVATAR_URLS = (
    "https://commons.wikimedia.org/wiki/Special:FilePath/Cliche_Hacker_and_Binary_Code_(26946304530).jpg",
    "https://upload.wikimedia.org/wikipedia/commons/f/ff/Cliche_Hacker_and_Binary_Code_(26946304530).jpg",
    "https://upload.wikimedia.org/wikipedia/commons/f/ff/Cliche_Hacker_and_Binary_Code_%2826946304530%29.jpg",
)
AVATAR_UA = "SynapseNetMeshPeer/1.0 (mesh identity; CC0 Wikimedia Commons avatar)"
# Last-resort 48x48 JPEG of the same CC0 crop if download/Pillow fail (under 80KB).
AVATAR_FALLBACK_JPEG_B64 = (
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAkGBwgHBgkIBwgKCgkLDRYPDQwMDRsUFRAWIB0iIiAdHx8kKDQsJCYxJx8fLT0tMTU3Ojo6Iys/RD84QzQ5Ojf/2wBDAQoKCg0MDRoPDxo3JR8lNzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzf/wAARCAAwADADASIAAhEBAxEB/8QAGwABAAMBAAMAAAAAAAAAAAAABAEFBgACAwf/xAAyEAABAwIEAwYEBwEAAAAAAAABAgMRAAQFEiExBkFhEyJRcYHhFDKh0RYjM0NiscHw/8QAFwEBAQEBAAAAAAAAAAAAAAAAAwQCAf/EACARAAICAQQDAQAAAAAAAAAAAAECABEDEhMhMSJBYQT/2gAMAwEAAhEDEQA/APj/APum9QCBUyDz36+9dPOeu/vVMGKw6xXfLd/MQ00y2XHXV/KhP3JMAcyaJdP2qXAm1Ly0jdawBPkK8by6IZ+Ea0bkKcM/qK5egnT1POg0bP6E0qm7MsEqChIMjeamevOj2ijChOgpE/y+vvW1Ni5wijOE/wDcvrTsKw749TpU+G0toKo3UsgEwkTvpQdt60N1dIwLh9u0S2k3t3lfU5m7zcjugR0jfxNLjUE23Qg5mYABOzM9dItHmibfOl1IkBSpzDWZ8CI9R5VXrSpCilaSlQ3BGopa8rb1wqQZScvUE0ZaysidhU+QC/sdLktOlsQANTrS0KzjMCY8/egUm1c/bPPauI3NTTD3HWYT8U1nQVJCh3Rz6bVaYhhbTyouFpQ4U5lPZico1kAc48fvWeF0EnMkZVJMjxqTfLJcJzkufOc5lWs/3TrlxhSGFwHxuWBBqaLAuDsRxjEGrRloHtUhReXADbewUsalPgBEk6DWhcX8Nfh66aQ1fM3zDqAoPNJKQDzEHwMiehBgivfhnHGLYXhl5YWZbSi7ydqtScyyEpI3O2pmeRFVD2KvXKlm6HbBasygVEanUx4SdamzPbAIOJX+dE0HdPlHYHwxc4xaKuUXDLCStaG+1nvlCM69hpAj1UKqLm2es3Qh0QqAoQZBHI1aI4kuW7JNo2hKW22nGmylRBSlakqPme7E8warnr1x9AQ73khefqTEb+QFF56vkWsO12dU/9k="
)
MAX_AVATAR_CHARS = 80000
PEX_GAP_SEC = 20
PEX_PEER_COOLDOWN = 300
PROFILE_COOLDOWN = 45
PEX_MAX_PER_SWEEP = 2

os.makedirs(STATE_DIR, exist_ok=True)
OWN_ONION = ""
AVATAR_DATAURL = ""
try:
    from nacl.public import PrivateKey, SealedBox

    HAS_NACL = True
except ImportError:
    HAS_NACL = False
BOX_SK = None
BOX_PK_HEX = ""
KEM_PK_HEX = ""
KEM_SK = b""
KEM_BACKEND = None
KEM_PK_LEN = 1184
KEM_SK_LEN = 2400
KEM_CT_LEN = 1088
MSG_KEM_LABEL = b"synapse-node-msg-v3"
MSG_PATH = os.path.join(STATE_DIR, "messages.jsonl")
PEERS = {}
PEERS_LOCK = threading.Lock()
PEX_LOCK = threading.Lock()
LAST_PEX_TS = 0.0
PEX_LAST = {}
PROFILE_LAST = {}
LOG_PATH = os.path.join(STATE_DIR, "mesh.log")
AVATAR_JPG = os.path.join(STATE_DIR, "avatar.jpg")
AVATAR_PNG = os.path.join(STATE_DIR, "avatar.png")
AVATAR_DATAURL_PATH = os.path.join(STATE_DIR, "avatar.dataurl")
AVATAR_SOURCE_PATH = os.path.join(STATE_DIR, "avatar.source")
PEERS_PATH = os.path.join(STATE_DIR, "peers.json")


def log(msg):
    line = time.strftime("%Y-%m-%d %H:%M:%S") + " " + msg
    print(line, flush=True)
    with open(LOG_PATH, "a") as f:
        f.write(line + "\n")


def onion_host(raw):
    s = (raw or "").strip()
    if s.startswith("http://"):
        s = s[7:]
    if ":" in s:
        host, port = s.rsplit(":", 1)
        if port.isdigit():
            s = host
    return s


def valid_v3(onion):
    o = onion_host(onion)
    if not o.endswith(".onion"):
        return False
    body = o[: -len(".onion")]
    if len(body) != 56:
        return False
    return all(c in "abcdefghijklmnopqrstuvwxyz234567" for c in body)


def load_peers():
    if not os.path.exists(PEERS_PATH):
        return
    try:
        rows = json.loads(open(PEERS_PATH).read() or "{}")
    except Exception:
        return
    if not isinstance(rows, dict):
        return
    with PEERS_LOCK:
        for onion, row in rows.items():
            o = onion_host(onion)
            if not valid_v3(o):
                continue
            if not isinstance(row, dict):
                row = {}
            row["onion"] = o
            PEERS[o] = row


def save_peers():
    with PEERS_LOCK:
        blob = json.dumps(PEERS, indent=0)
    tmp = PEERS_PATH + ".tmp"
    with open(tmp, "w") as f:
        f.write(blob)
    os.replace(tmp, PEERS_PATH)


def remember(onion, source="inbound"):
    o = onion_host(onion)
    if not valid_v3(o) or o == OWN_ONION:
        return
    with PEERS_LOCK:
        row = PEERS.get(o, {})
        row["onion"] = o
        row["source"] = source
        row["last"] = int(time.time())
        PEERS[o] = row
    try:
        save_peers()
    except Exception:
        pass


def recvn(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise OSError("socks eof")
        buf += chunk
    return buf


def socks5_connect(onion, port=P2P_PORT, timeout=25):
    # Same handshake as synapsed_engine sendOnionPayload: no-auth + user/pass, CONNECT domain.
    s = socket.create_connection((SOCKS_HOST, SOCKS_PORT), timeout=timeout)
    s.settimeout(timeout)
    try:
        s.sendall(b"\x05\x02\x00\x02")
        g = recvn(s, 2)
        if g[0] != 5:
            raise OSError("socks version")
        if g[1] == 2:
            s.sendall(b"\x01\x00\x00")
            auth = recvn(s, 2)
            if auth[1] != 0:
                raise OSError("socks auth")
        elif g[1] != 0:
            raise OSError("socks method %d" % g[1])
        host = onion.encode("ascii")
        req = b"\x05\x01\x00\x03" + bytes([len(host)]) + host
        req += bytes([(port >> 8) & 0xFF, port & 0xFF])
        s.sendall(req)
        hdr = recvn(s, 4)
        if hdr[1] != 0:
            raise OSError("socks connect rep=%d" % hdr[1])
        atyp = hdr[3]
        if atyp == 1:
            recvn(s, 6)
        elif atyp == 3:
            ln = recvn(s, 1)[0]
            recvn(s, ln + 2)
        elif atyp == 4:
            recvn(s, 18)
        else:
            recvn(s, 6)
        return s
    except Exception:
        try:
            s.close()
        except OSError:
            pass
        raise


def socks5_send(onion, payload, wait_ack=True):
    s = None
    try:
        s = socks5_connect(onion, P2P_PORT)
        if not isinstance(payload, bytes):
            payload = payload.encode("utf-8")
        s.sendall(payload)
        if wait_ack:
            try:
                s.recv(256)
            except (socket.timeout, OSError):
                pass
        return True
    except Exception as e:
        log("socks send %s: %s" % (onion, e))
        return False
    finally:
        if s is not None:
            try:
                s.close()
            except OSError:
                pass


def dataurl_ok(url):
    if not url or not url.startswith("data:image/"):
        return False
    if len(url) > MAX_AVATAR_CHARS:
        return False
    return True


def jpeg_to_dataurl(jpeg_bytes):
    return "data:image/jpeg;base64," + base64.b64encode(jpeg_bytes).decode("ascii")


def fallback_dataurl():
    return "data:image/jpeg;base64," + AVATAR_FALLBACK_JPEG_B64


def write_avatar_source(url):
    with open(AVATAR_SOURCE_PATH, "w") as f:
        f.write("page=" + AVATAR_PAGE + "\n")
        f.write("url=" + (url or AVATAR_URLS[0]) + "\n")
        f.write("license=CC0-1.0\n")


def persist_dataurl(url):
    tmp = AVATAR_DATAURL_PATH + ".tmp"
    with open(tmp, "w") as f:
        f.write(url)
    os.replace(tmp, AVATAR_DATAURL_PATH)


def pillow_square_jpeg(src_path):
    from PIL import Image

    try:
        resample = Image.Resampling.LANCZOS
    except AttributeError:
        resample = Image.LANCZOS
    img = Image.open(src_path).convert("RGB")
    w, h = img.size
    side = min(w, h)
    if side < 8:
        raise RuntimeError("avatar too small")
    left = (w - side) // 2
    top = (h - side) // 2
    sq = img.crop((left, top, left + side, top + side))
    mesh = sq.resize((48, 48), resample)
    buf = io.BytesIO()
    mesh.save(buf, format="JPEG", quality=72, optimize=True)
    jpeg = buf.getvalue()
    mesh.save(AVATAR_JPG, format="JPEG", quality=72, optimize=True)
    try:
        sq.resize((128, 128), resample).save(AVATAR_PNG, format="PNG")
    except Exception:
        pass
    return jpeg


def download_avatar(dest):
    last_err = None
    for url in AVATAR_URLS:
        try:
            req = urllib.request.Request(
                url,
                headers={
                    "User-Agent": AVATAR_UA,
                    "Accept": "image/jpeg,image/*;q=0.8,*/*;q=0.5",
                },
            )
            with urllib.request.urlopen(req, timeout=30) as resp:
                data = resp.read()
            if len(data) < 200 or data[:2] != b"\xff\xd8":
                raise RuntimeError("not a jpeg (%d bytes)" % len(data))
            with open(dest, "wb") as f:
                f.write(data)
            write_avatar_source(url)
            log("avatar downloaded %d bytes from %s" % (len(data), url))
            return url
        except Exception as e:
            last_err = e
            log("avatar urllib fail %s: %s" % (url, e))
        try:
            subprocess.check_call(
                [
                    "curl",
                    "-fsSL",
                    "-A",
                    AVATAR_UA,
                    "--max-time",
                    "30",
                    "-o",
                    dest,
                    url,
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            data = open(dest, "rb").read()
            if len(data) < 200 or data[:2] != b"\xff\xd8":
                raise RuntimeError("curl not a jpeg")
            write_avatar_source(url)
            log("avatar curl %d bytes from %s" % (len(data), url))
            return url
        except Exception as e:
            last_err = e
            log("avatar curl fail %s: %s" % (url, e))
    raise RuntimeError("avatar download failed: %s" % last_err)


def jpeg_file_ok(path, min_bytes=8000):
    if not path or not os.path.exists(path):
        return False
    try:
        if os.path.getsize(path) < min_bytes:
            return False
        with open(path, "rb") as f:
            return f.read(2) == b"\xff\xd8"
    except OSError:
        return False


def load_or_build_avatar():
    global AVATAR_DATAURL
    tmp_src = os.path.join(STATE_DIR, "avatar.src.jpg")
    # Tiny fallback JPEG / data URL must not win over a real Commons download.
    if os.path.exists(AVATAR_DATAURL_PATH) and not jpeg_file_ok(tmp_src):
        cached = open(AVATAR_DATAURL_PATH).read().strip()
        if dataurl_ok(cached) and len(cached) > 4000:
            AVATAR_DATAURL = cached
            log("avatar dataurl cache %d chars" % len(cached))
            return
    src = ""
    if jpeg_file_ok(tmp_src):
        src = tmp_src
    elif jpeg_file_ok(AVATAR_JPG):
        src = AVATAR_JPG
    else:
        try:
            download_avatar(tmp_src)
            src = tmp_src
        except Exception as e:
            log("avatar download skipped: " + str(e))
            src = AVATAR_JPG if os.path.exists(AVATAR_JPG) else ""
    jpeg = b""
    if src and os.path.exists(src):
        try:
            jpeg = pillow_square_jpeg(src)
            log("avatar pillow 48x48 %d bytes" % len(jpeg))
        except Exception as e:
            log("avatar pillow unavailable: " + str(e))
            raw = open(src, "rb").read()
            # Raw original is often >80KB as a data URL; only use if it fits.
            trial = jpeg_to_dataurl(raw)
            if dataurl_ok(trial):
                jpeg = raw
                if src != AVATAR_JPG:
                    open(AVATAR_JPG, "wb").write(raw)
    if jpeg:
        url = jpeg_to_dataurl(jpeg)
        if dataurl_ok(url):
            AVATAR_DATAURL = url
            persist_dataurl(url)
            write_avatar_source(AVATAR_URLS[0])
            log("avatar ready %d chars" % len(url))
            return
    AVATAR_DATAURL = fallback_dataurl()
    persist_dataurl(AVATAR_DATAURL)
    write_avatar_source(AVATAR_URLS[0])
    if not os.path.exists(AVATAR_JPG):
        open(AVATAR_JPG, "wb").write(base64.b64decode(AVATAR_FALLBACK_JPEG_B64))
    log("avatar fallback 48x48 CC0 crop %d chars" % len(AVATAR_DATAURL))


def load_or_make_box():
    global BOX_SK, BOX_PK_HEX
    if not HAS_NACL:
        log("nacl missing: sealed MSG disabled")
        return
    path = os.path.join(STATE_DIR, "msgbox.key")
    sk = None
    if os.path.exists(path):
        lines = [ln.strip() for ln in open(path) if ln.strip()]
        if lines and len(lines[0]) == 64:
            try:
                sk = PrivateKey(bytes.fromhex(lines[0]))
            except Exception as e:
                log("msgbox load fail: " + str(e))
    if sk is None:
        sk = PrivateKey.generate()
        pkhex = sk.public_key.encode().hex()
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            f.write(sk.encode().hex() + "\n" + pkhex + "\n")
        os.replace(tmp, path)
        os.chmod(path, 0o600)
        log("msgbox generated")
    BOX_SK = sk
    BOX_PK_HEX = sk.public_key.encode().hex()
    log("msgbox pk ready")


def _kem_close(obj):
    for name in ("free", "close"):
        fn = getattr(obj, name, None)
        if callable(fn):
            try:
                fn()
            except Exception:
                pass
            return


class _OqsKem:
    def __init__(self):
        import oqs

        self.oqs = oqs
        self.name = None
        for alg in ("ML-KEM-768", "Kyber768"):
            try:
                k = oqs.KeyEncapsulation(alg)
                _kem_close(k)
                self.name = alg
                break
            except Exception:
                continue
        if not self.name:
            raise RuntimeError("no ML-KEM-768")

    def keypair(self):
        k = self.oqs.KeyEncapsulation(self.name)
        try:
            pk = bytes(k.generate_keypair())
            sk = bytes(k.export_secret_key())
            return pk, sk
        finally:
            _kem_close(k)

    def decaps(self, ct, sk):
        k = self.oqs.KeyEncapsulation(self.name, secret_key=sk)
        try:
            return bytes(k.decap_secret(ct))
        finally:
            _kem_close(k)


class _CtypesKem:
    def __init__(self, lib):
        self.lib = lib
        lib.OQS_KEM_new.restype = ctypes.c_void_p
        lib.OQS_KEM_new.argtypes = [ctypes.c_char_p]
        lib.OQS_KEM_free.argtypes = [ctypes.c_void_p]
        lib.OQS_KEM_keypair.restype = ctypes.c_int
        lib.OQS_KEM_keypair.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
        ]
        lib.OQS_KEM_decaps.restype = ctypes.c_int
        lib.OQS_KEM_decaps.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
        ]
        self.name = None
        for alg in (b"ML-KEM-768", b"Kyber768"):
            kem = lib.OQS_KEM_new(alg)
            if kem:
                lib.OQS_KEM_free(kem)
                self.name = alg
                break
        if not self.name:
            raise RuntimeError("no ML-KEM-768")

    def _kem(self):
        kem = self.lib.OQS_KEM_new(self.name)
        if not kem:
            raise RuntimeError("OQS_KEM_new failed")
        return kem

    def keypair(self):
        kem = self._kem()
        try:
            pk = (ctypes.c_uint8 * KEM_PK_LEN)()
            sk = (ctypes.c_uint8 * KEM_SK_LEN)()
            if self.lib.OQS_KEM_keypair(kem, pk, sk) != 0:
                raise RuntimeError("keypair failed")
            return bytes(pk), bytes(sk)
        finally:
            self.lib.OQS_KEM_free(kem)

    def decaps(self, ct, sk):
        if len(ct) != KEM_CT_LEN or len(sk) != KEM_SK_LEN:
            raise ValueError("kem size")
        kem = self._kem()
        try:
            ss = (ctypes.c_uint8 * 32)()
            ctb = (ctypes.c_uint8 * KEM_CT_LEN).from_buffer_copy(ct)
            skb = (ctypes.c_uint8 * KEM_SK_LEN).from_buffer_copy(sk)
            if self.lib.OQS_KEM_decaps(kem, ss, ctb, skb) != 0:
                raise RuntimeError("decaps failed")
            return bytes(ss)
        finally:
            self.lib.OQS_KEM_free(kem)


def _load_liboqs():
    names = []
    found = ctypes.util.find_library("oqs")
    if found:
        names.append(found)
    names.extend(
        (
            "liboqs.so",
            "liboqs.so.7",
            "liboqs.so.6",
            "liboqs.so.5",
            "/usr/local/lib/liboqs.so",
            "/usr/lib/liboqs.so",
            "/usr/lib/x86_64-linux-gnu/liboqs.so",
        )
    )
    seen = set()
    for n in names:
        if not n or n in seen:
            continue
        seen.add(n)
        try:
            return ctypes.CDLL(n)
        except OSError:
            continue
    return None


def load_or_make_kem():
    global KEM_PK_HEX, KEM_SK, KEM_BACKEND
    backend = None
    try:
        backend = _OqsKem()
        log("kem backend python oqs")
    except Exception:
        lib = _load_liboqs()
        if lib is not None:
            try:
                backend = _CtypesKem(lib)
                log("kem backend ctypes liboqs")
            except Exception as e:
                log("liboqs ctypes fail: " + type(e).__name__)
    if backend is None:
        log("kem unavailable: NODE_MSG stays X25519 v2")
        return
    path = os.path.join(STATE_DIR, "msgkem.key")
    sk = b""
    pk = b""
    if os.path.exists(path):
        lines = [ln.strip() for ln in open(path) if ln.strip()]
        if (
            len(lines) >= 2
            and len(lines[0]) == KEM_SK_LEN * 2
            and len(lines[1]) == KEM_PK_LEN * 2
        ):
            try:
                sk = bytes.fromhex(lines[0])
                pk = bytes.fromhex(lines[1])
            except Exception:
                log("msgkem load fail")
                sk, pk = b"", b""
    if len(sk) != KEM_SK_LEN or len(pk) != KEM_PK_LEN:
        pk, sk = backend.keypair()
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            f.write(sk.hex() + "\n" + pk.hex() + "\n")
        os.replace(tmp, path)
        os.chmod(path, 0o600)
        log("msgkem generated")
    KEM_BACKEND = backend
    KEM_SK = sk
    KEM_PK_HEX = pk.hex()
    log("msgkem pk ready")


def profile_line():
    body = {
        "v": 1,
        "onion": OWN_ONION,
        "alias": ALIAS,
        "avatar": AVATAR_DATAURL if dataurl_ok(AVATAR_DATAURL) else "",
        "box_pk": BOX_PK_HEX,
    }
    if KEM_PK_HEX:
        body["kem_pk"] = KEM_PK_HEX
    return "NODE_PROFILE " + json.dumps(body, separators=(",", ":")) + "\n"


def push_own_profile(onion):
    o = onion_host(onion)
    if not valid_v3(o) or o == OWN_ONION or not OWN_ONION:
        return False
    now = time.time()
    with PEX_LOCK:
        if now - PROFILE_LAST.get(o, 0) < PROFILE_COOLDOWN:
            return False
        PROFILE_LAST[o] = now
    ok = socks5_send(o, profile_line())
    log("NODE_PROFILE push %s %s" % (o, "ok" if ok else "fail"))
    return ok


def pex_get_peers(onion):
    o = onion_host(onion)
    if not valid_v3(o) or o == OWN_ONION or not OWN_ONION:
        return []
    s = None
    learned = []
    data = b""
    try:
        s = socks5_connect(o, P2P_PORT)
        s.sendall(("GET_PEERS " + OWN_ONION + "\n").encode("ascii"))
        while b"\n" not in data and len(data) < 8192:
            chunk = s.recv(1024)
            if not chunk:
                break
            data += chunk
    finally:
        if s is not None:
            try:
                s.close()
            except OSError:
                pass
    text = data.decode("utf-8", "replace") if data else ""
    if text.startswith("PEERS"):
        for tok in text[5:].split():
            other = onion_host(tok)
            if valid_v3(other) and other != OWN_ONION:
                remember(other, "pex")
                learned.append(other)
    remember(o, "pex")
    return learned


def maybe_pex(onion):
    global LAST_PEX_TS
    o = onion_host(onion)
    if not valid_v3(o) or o == OWN_ONION:
        return
    now = time.time()
    with PEX_LOCK:
        if now - LAST_PEX_TS < PEX_GAP_SEC:
            return
        if now - PEX_LAST.get(o, 0) < PEX_PEER_COOLDOWN:
            return
        LAST_PEX_TS = now
        PEX_LAST[o] = now
    try:
        learned = pex_get_peers(o)
        log("pex %s learned=%d" % (o, len(learned)))
        push_own_profile(o)
    except Exception as e:
        log("pex fail %s: %s" % (o, e))


def after_handshake(onion):
    o = onion_host(onion)
    if not valid_v3(o):
        return

    def run():
        # Let the inbound reply close before we circuit back through SOCKS.
        time.sleep(0.4)
        push_own_profile(o)
        maybe_pex(o)

    threading.Thread(target=run, name="mesh-hs-" + o[:6], daemon=True).start()


def pex_sweep():
    with PEERS_LOCK:
        onions = list(PEERS.keys())
    n = 0
    for o in onions:
        if n >= PEX_MAX_PER_SWEEP:
            break
        before = LAST_PEX_TS
        maybe_pex(o)
        if LAST_PEX_TS != before:
            n += 1


class TorControl:
    def __init__(self):
        self.sock = socket.create_connection((CONTROL_HOST, CONTROL_PORT), timeout=20)
        self.buf = b""
        self.auth()

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def _read_lines(self):
        lines = []
        while True:
            while b"\n" not in self.buf:
                chunk = self.sock.recv(4096)
                if not chunk:
                    raise RuntimeError("control closed")
                self.buf += chunk
            line, self.buf = self.buf.split(b"\n", 1)
            text = line.decode("ascii", "replace").rstrip("\r")
            lines.append(text)
            if len(text) >= 4 and text[3] == " ":
                return lines

    def cmd(self, line):
        self.sock.sendall((line + "\r\n").encode("ascii"))
        return self._read_lines()

    def auth(self):
        cookie = b""
        for path in COOKIE_PATHS:
            if os.path.exists(path):
                with open(path, "rb") as f:
                    cookie = f.read()
                break
        if cookie:
            lines = self.cmd("AUTHENTICATE " + cookie.hex())
        else:
            lines = self.cmd("AUTHENTICATE")
        if not lines or not lines[-1].startswith("250"):
            raise RuntimeError("AUTHENTICATE failed: " + repr(lines))

    def wait_bootstrap(self, timeout=180):
        deadline = time.time() + timeout
        while time.time() < deadline:
            lines = self.cmd("GETINFO status/bootstrap-phase")
            blob = " ".join(lines)
            if "PROGRESS=100" in blob or "TAG=done" in blob:
                return
            time.sleep(2)
        raise RuntimeError("tor bootstrap timeout")

    def add_onion(self):
        key_path = os.path.join(STATE_DIR, "onion.key")
        key_spec = "NEW:ED25519-V3"
        if os.path.exists(key_path):
            key_spec = open(key_path).read().strip()
        ports = " Port=8333,127.0.0.1:%d Port=8332,127.0.0.1:%d" % (P2P_PORT, RPC_PORT)
        lines = self.cmd("ADD_ONION " + key_spec + ports)
        service = ""
        priv = ""
        ok = False
        for line in lines:
            if line.startswith("250-ServiceID="):
                service = line.split("=", 1)[1].strip()
            elif line.startswith("250-PrivateKey="):
                priv = line.split("=", 1)[1].strip()
            elif line.startswith("250 "):
                ok = True
        if not ok or not service:
            raise RuntimeError("ADD_ONION failed: " + repr(lines))
        if priv:
            with open(key_path, "w") as f:
                f.write(priv + "\n")
            os.chmod(key_path, 0o600)
        onion = service + ".onion"
        open(os.path.join(STATE_DIR, "session.onion"), "w").write(onion + "\n")
        return onion


def handle_p2p(conn, addr):
    conn.settimeout(20)
    try:
        data = b""
        while b"\n" not in data and len(data) < 98304:
            chunk = conn.recv(4096)
            if not chunk:
                break
            data += chunk
        msg = data.decode("utf-8", "replace")
        if msg.startswith("GET_PEERS"):
            rest = msg[9:].strip().split()
            inbound = ""
            if rest:
                inbound = onion_host(rest[0])
                remember(inbound, "inbound")
                log("GET_PEERS from " + inbound)
            with PEERS_LOCK:
                onions = list(PEERS.keys())
            reply = "PEERS"
            if OWN_ONION:
                reply += " " + OWN_ONION
            for o in onions[:50]:
                if o != OWN_ONION and o != inbound:
                    reply += " " + o
            conn.sendall((reply + "\n").encode("ascii"))
            if inbound:
                after_handshake(inbound)
        elif msg.startswith("SYNAPSE_PEER "):
            peer = onion_host(msg[13:])
            remember(peer, "inbound")
            log("SYNAPSE_PEER " + peer)
            conn.sendall(("SYNAPSE_ACK " + OWN_ONION + ":8333\n").encode("ascii"))
            after_handshake(peer)
        elif msg.startswith("NODE_PROFILE "):
            body = msg[13:].strip()
            try:
                j = json.loads(body)
                o = onion_host(j.get("onion", ""))
                remember(o, "profile")
                if valid_v3(o):
                    with PEERS_LOCK:
                        row = PEERS.get(o, {"onion": o})
                        alias = str(j.get("alias", "") or "")[:32]
                        if alias:
                            row["alias"] = alias
                        bpk = str(j.get("box_pk", "") or "")
                        if len(bpk) == 64:
                            row["box_pk"] = bpk
                        kpk = str(j.get("kem_pk", "") or "")
                        if len(kpk) == KEM_PK_LEN * 2:
                            row["kem_pk"] = kpk
                        PEERS[o] = row
                log("NODE_PROFILE " + o + " alias=" + str(j.get("alias", "")))
            except Exception:
                pass
            conn.sendall(b"PROFILE_ACK\n")
        elif msg.startswith("NODE_MSG "):
            raw = msg[9:].strip()
            ack_id = ""
            try:
                j = json.loads(raw)
                ack_id = str(j.get("id", "") or "")
                if j.get("body") and not j.get("seal"):
                    log("NODE_MSG drop plaintext")
                elif BOX_SK is None:
                    log("NODE_MSG no box key")
                else:
                    seal_b64 = j.get("seal") or ""
                    kem_b64 = j.get("kem") or ""
                    seal_raw = base64.b64decode(seal_b64)
                    hybrid = bool(kem_b64)
                    if hybrid:
                        if KEM_BACKEND is None or not KEM_SK:
                            log("NODE_MSG v3 kem unavailable")
                            raise RuntimeError("kem")
                        ct = base64.b64decode(kem_b64)
                        ss = KEM_BACKEND.decaps(ct, KEM_SK)
                        from nacl.secret import SecretBox

                        key = hashlib.sha256(ss + MSG_KEM_LABEL).digest()
                        seal_raw = SecretBox(key).decrypt(seal_raw)
                    pt = SealedBox(BOX_SK).decrypt(seal_raw)
                    inner = json.loads(pt.decode("utf-8"))
                    text = str(inner.get("body", "") or "")
                    frm = onion_host(str(j.get("from") or inner.get("from") or ""))
                    remember(frm, "msg")
                    rec = {
                        "peer": frm,
                        "from": frm,
                        "to": OWN_ONION,
                        "dir": "in",
                        "body": text,
                        "ts": int(time.time() * 1000),
                        "encrypted": True,
                        "hybrid": hybrid,
                    }
                    with open(MSG_PATH, "a") as f:
                        f.write(json.dumps(rec, separators=(",", ":")) + "\n")
                    log(
                        "NODE_MSG sealed ok from=%s bytes=%d hybrid=%s"
                        % (frm, len(text), hybrid)
                    )
            except Exception as e:
                log("NODE_MSG fail: " + type(e).__name__)
            conn.sendall(("MSG_ACK " + ack_id + "\n").encode("ascii"))
        elif msg.startswith("RELAY_TX"):
            conn.sendall(b"TX_ACK\n")
        else:
            log("unknown p2p from %s: %r" % (addr, msg[:80]))
    except Exception as e:
        log("p2p error: " + str(e))
    finally:
        try:
            conn.close()
        except OSError:
            pass


def p2p_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", P2P_PORT))
    srv.listen(32)
    log("p2p listen 127.0.0.1:%d" % P2P_PORT)
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=handle_p2p, args=(conn, addr), daemon=True).start()


class RpcHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        log("rpc " + (fmt % args))

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(n) if n else b"{}"
        try:
            req = json.loads(raw.decode("utf-8") or "{}")
        except Exception:
            req = {}
        method = req.get("method", "")
        params = req.get("params") or {}
        rid = req.get("id", 1)
        if method == "peer.announce":
            onion = onion_host(params.get("onion", ""))
            remember(onion, "announce")
            result = {"ok": True, "onion": onion}
            if valid_v3(onion):
                after_handshake(onion)
        elif method == "peer.directory":
            with PEERS_LOCK:
                peers = [{"onion": o} for o in PEERS.keys()]
            if OWN_ONION:
                peers.insert(0, {"onion": OWN_ONION, "alias": ALIAS})
            result = {"peers": peers}
        elif method == "blocks.list":
            result = {"blocks": [], "height": 0}
        else:
            result = {"error": "unknown method", "method": method}
        body = json.dumps({"jsonrpc": "2.0", "id": rid, "result": result}).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)


def rpc_server():
    httpd = ThreadingHTTPServer(("127.0.0.1", RPC_PORT), RpcHandler)
    log("rpc listen 127.0.0.1:%d" % RPC_PORT)
    httpd.serve_forever()


def main():
    global OWN_ONION
    log("mesh peer starting alias=" + ALIAS)
    load_peers()
    load_or_build_avatar()
    load_or_make_box()
    load_or_make_kem()
    # ADD_ONION lives only while this control connection stays open.
    ctl = None
    for attempt in range(30):
        try:
            ctl = TorControl()
            break
        except OSError as e:
            log("control connect retry: " + str(e))
            time.sleep(2)
    if ctl is None:
        raise RuntimeError("tor control unreachable")
    ctl.wait_bootstrap()
    OWN_ONION = ctl.add_onion()
    log("onion " + OWN_ONION)
    threading.Thread(target=p2p_server, daemon=True).start()
    threading.Thread(target=rpc_server, daemon=True).start()
    try:
        while True:
            time.sleep(30)
            with PEERS_LOCK:
                n = len(PEERS)
                sample = ",".join(list(PEERS.keys())[:5])
            try:
                lines = ctl.cmd("GETINFO onions/current")
                log("onions/current " + " | ".join(lines)[:200])
            except Exception as e:
                log("control keepalive failed: " + str(e))
                raise
            try:
                pex_sweep()
            except Exception as e:
                log("pex sweep: " + str(e))
            log("heartbeat onion=%s peers=%d %s" % (OWN_ONION, n, sample))
    finally:
        ctl.close()


if __name__ == "__main__":
    main()
