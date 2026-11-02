#!/usr/bin/env python3
"""The PS3's invite codes (p2p_crypto.c, tools/ps3_invite_code_test.c; host cc, else the ps3dev image) against Python's
and, with node, port/ps3/invite/index.html's own, both ways. Exit status: 0 passed, 1 failed, 2 not run."""

import hashlib
import hmac
import json
import os
import secrets
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = "ps3dev-latest:latest"
PAGE = ROOT / "port/ps3/invite/index.html"
SOURCES = ["tools/ps3_invite_code_test.c", "chupathingyce/port/third_party/monocypher/monocypher.c",
           "chupathingyce/port/third_party/monocypher/monocypher-ed25519.c"]
FLAGS = ["-O2", "-Wall", "-Ichupathingyce/port/linux/src", "-Ichupathingyce/port/third_party/monocypher"]

SALT = b"hceu invite code 1"
ROUNDS = 100000

# the page's core script run by node, its WebSocket a broker in memory (MQTT 5: CONNACK, SUBACK with the retained
# message, PUBACK): its key, topic, seal, open and parsers; Link to code and Code to link through that broker
NODE_SCRIPT = r"""
const fs = require('fs');
const bytes = (text) => Uint8Array.from(text.match(/../g) || [], (pair) => parseInt(pair, 16));
const retained = new Map();
const decoder = new TextDecoder();
class FakeSocket {
  constructor() {
    this.readyState = 0;
    setTimeout(() => { this.readyState = 1; this.onopen(); }, 0);
  }
  reply(packet) {
    setTimeout(() => this.onmessage && this.onmessage({ data: Uint8Array.from(packet).buffer }), 0);
  }
  send(data) {
    const packet = new Uint8Array(data);
    let at = 1, length = 0, shift = 0;
    do { length |= (packet[at] & 127) << shift; shift += 7; } while (packet[at++] & 128);
    const body = packet.subarray(at, at + length);
    const type = packet[0] >> 4;
    const text = (at) => decoder.decode(body.subarray(at + 2, at + 2 + ((body[at] << 8) | body[at + 1])));
    if (type === 1) this.reply([0x20, 3, 0, 0, 0]);
    if (type === 8) {
      const topic = text(3);
      this.reply([0x90, 4, body[0], body[1], 0, 0]);
      const payload = retained.get(topic);
      const name = new TextEncoder().encode(topic);
      if (payload) this.reply([0x31, 2 + name.length + 1 + payload.length, name.length >> 8, name.length & 255,
        ...name, 0, ...payload]);
    }
    if (type === 3) {
      const topic = text(0);
      let next = 2 + ((body[0] << 8) | body[1]);
      const identifier = [body[next], body[next + 1]];
      next += 2;
      next += 1 + body[next];
      retained.set(topic, body.slice(next));
      this.reply([0x40, 2, ...identifier]);
    }
  }
  close() { this.readyState = 3; }
}
globalThis.WebSocket = FakeSocket;
(async () => {
  const input = JSON.parse(fs.readFileSync(0, 'utf8'));
  const core = /<script id="core">([\s\S]*?)<\/script>/.exec(fs.readFileSync(input.page, 'utf8'))[1];
  const InviteCodes = new Function(core + '\nreturn InviteCodes;')();
  const hex = InviteCodes.hex;
  const output = { cases: [], made: [], found: [] };
  for (const [digits, invite, payload] of input.cases || []) {
    const key = await InviteCodes.codeKey(digits);
    const opened = await InviteCodes.open(key, bytes(payload));
    output.cases.push({ key: hex(key), topic: await InviteCodes.codeTopic(key),
      sealed: hex(await InviteCodes.seal(key, bytes(invite))), opened: opened && hex(opened) });
  }
  output.links = (input.links || []).map((link) => {
    const invite = InviteCodes.parseLink(link);
    return invite && hex(invite);
  });
  output.codes = (input.codes || []).map((code) => InviteCodes.parseCode(code));
  // (no brokers: only the messages for links that make no code)
  output.refusals = [];
  for (const link of (input.links || []).slice(2))
    output.refusals.push(await InviteCodes.makeInviteCode(link, []).then(() => '', (error) => error.message));
  for (const link of input.make || []) {
    retained.clear();
    const { code } = await InviteCodes.makeInviteCode(link, ['wss://memory/mqtt']);
    const [[topic, payload]] = [...retained];
    output.made.push({ code, topic, payload: hex(payload) });
  }
  for (const [digits, topic, payload, typed] of input.find || []) {
    retained.clear();
    retained.set(topic, bytes(payload));
    output.found.push(await InviteCodes.findInviteLink(typed, ['wss://memory/mqtt']));
  }
  output.missing = input.find ? await InviteCodes.findInviteLink('0000 0001', []) : null;
  output.badCode = await InviteCodes.findInviteLink('123', []).then(() => '', (error) => error.message);
  process.stdout.write(JSON.stringify(output));
})().catch((error) => { console.error(error); process.exit(1); });
"""


def code_key(digits):
    return hashlib.pbkdf2_hmac("sha256", digits.encode(), SALT, ROUNDS, 32)


def code_hash(key, label, data=b""):
    return hmac.new(key, label.encode() + data, hashlib.sha256).digest()


def code_topic(key):
    return "hceu/3/code/" + code_hash(key, "topic")[:16].hex()


def seal(key, invite):
    nonce = secrets.token_bytes(16)
    sealed = bytes(a ^ b for a, b in zip(invite, code_hash(key, "stream", nonce)))
    return b"C\x01" + nonce + sealed + code_hash(key, "tag", nonce + sealed)[:16]


def open_payload(key, payload):
    """Python's own open, as the page's: the invite, or None"""
    if len(payload) != 66 or payload[:2] != b"C\x01":
        return None
    nonce, sealed, tag = payload[2:18], payload[18:50], payload[50:]
    if not hmac.compare_digest(code_hash(key, "tag", nonce + sealed)[:16], tag):
        return None
    return bytes(a ^ b for a, b in zip(sealed, code_hash(key, "stream", nonce)))


def flipped(payload, index):
    return payload[:index] + bytes([payload[index] ^ 1]) + payload[index + 1:]


def native(path):
    """the path as Windows programs (docker, node) take it under Git Bash's Python"""
    cygpath = shutil.which("cygpath")
    if cygpath:
        return subprocess.run([cygpath, "-m", str(path)], capture_output=True, text=True).stdout.strip()
    return str(path)


def run_harness(lines):
    """each line: (digits, payload to open) or ("seal", digits, invite); each result: ms, key, topic, invite or
    payload"""
    text = "".join(f"seal {line[1]} {line[2].hex()}\n" if line[0] == "seal" else f"{line[0]} {line[1].hex() or '-'}\n"
                   for line in lines)
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    with tempfile.TemporaryDirectory() as folder:
        program = str(Path(folder) / "ps3_invite_code_test")
        if compiler and subprocess.run([compiler, *FLAGS, "-o", program, *SOURCES], cwd=ROOT,
                                       capture_output=True).returncode == 0:
            print(f"harness: {compiler}")
            result = subprocess.run([program], input=text, capture_output=True, text=True)
        elif shutil.which("docker"):
            print(f"harness: gcc in {IMAGE}")
            build = f"gcc {' '.join(FLAGS)} -o /tmp/t {' '.join(SOURCES)} && /tmp/t"
            # (//src: Git Bash's Python would turn /src into a Windows path)
            result = subprocess.run(["docker", "run", "--rm", "-i", "-v", f"{native(ROOT)}:/src", "-w", "//src", IMAGE,
                                     "sh", "-c", build], input=text, capture_output=True, text=True)
        else:
            print("no C compiler and no docker: not run", file=sys.stderr)
            sys.exit(2)
    if result.returncode != 0:
        print(result.stdout + result.stderr, file=sys.stderr)
        sys.exit(2)
    return [line.split() for line in result.stdout.splitlines()]


def run_page(request):
    node = shutil.which("node")
    if not node:
        return None
    result = subprocess.run([node, "-e", NODE_SCRIPT], capture_output=True, text=True,
                            input=json.dumps({"page": native(PAGE), **request}))
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(2)
    return json.loads(result.stdout)


def main():
    failures = []
    checks = 0

    def check(condition, what):
        nonlocal checks
        checks += 1
        if not condition:
            failures.append(what)

    codes = ["00000000", "00012345", "12345678", "99999999"] + [f"{secrets.randbelow(10 ** 8):08d}" for _ in range(2)]
    keys = {code: code_key(code) for code in codes}
    invites = {code: secrets.token_bytes(32) for code in codes}
    payloads = {code: seal(keys[code], invites[code]) for code in codes}
    # each line: digits, payload, the invite it must open to (None: none); or a seal, its invite
    lines = [(code, payloads[code], invites[code]) for code in codes]
    first, second = codes[2], codes[3]
    good = payloads[first]
    lines += [
        (first, flipped(good, 5), None),
        (first, flipped(good, 2 + 16 + 3), None),
        (first, flipped(good, len(good) - 1), None),
        (first, good[:1] + b"\x02" + good[2:], None),
        (first, b"D" + good[1:], None),
        (first, good[:-1], None),
        (first, good + b"\x00", None),
        (first, b"", None),
        (second, good, None),
    ]
    seals = [("seal", code, invites[code]) for code in codes]

    # Link to code: the page's seal, and a code it makes and publishes, which the PS3 must open
    page_link = secrets.token_bytes(32)
    page = run_page({"cases": [[code, invites[code].hex(), payloads[code].hex()] for code in codes],
                     "links": [f"halo://join/{invites[first].hex()}",
                               f"Join me: halo://join/{invites[first].hex().upper()} !",
                               f"halo://join/p{secrets.token_bytes(36).hex()}", "halo://join/1234"],
                     "codes": ["1234 5678", "1234-5678", "12345678", "1234567", "1234 567a"],
                     "make": [f"halo://join/{page_link.hex()}"]})
    made = None
    if page:
        for code, result in zip(codes, page["cases"]):
            check(result["key"] == keys[code].hex(), f"page key of {code}")
            check(result["topic"] == code_topic(keys[code]), f"page topic of {code}")
            check(result["opened"] == invites[code].hex(), f"page open of Python's payload for {code}")
            lines.append((code, bytes.fromhex(result["sealed"]), invites[code]))
        check(page["links"][:2] == [invites[first].hex()] * 2, "page link parse")
        check(page["links"][2:] == [None, None], "page refuses a locked or short link")
        check("password" in page["refusals"][0] and "not an invite link" in page["refusals"][1],
              "page's messages for a locked and a short link")
        check(page["codes"] == ["12345678"] * 3 + [None, None], "page code parse")
        made = page["made"][0]
        digits = made["code"].replace(" ", "")
        check(len(made["code"]) == 9 and digits.isdigit(), "page's code is 8 digits, spaced")
        check(made["topic"] == code_topic(code_key(digits)), "page publishes under the code's topic")
        lines.append((digits, bytes.fromhex(made["payload"]), page_link))
    else:
        print("node not found: the page is not checked")

    results = run_harness([line[:2] for line in lines] + seals)
    check(len(results) == len(lines) + len(seals), "a harness line for each case")
    times = []
    for (digits, payload, invite), result in zip(lines, results):
        milliseconds, key, topic, opened = result
        times.append(int(milliseconds))
        check(key == code_key(digits).hex(), f"key of {digits}")
        check(topic == code_topic(code_key(digits)), f"topic of {digits}")
        check(opened == (invite.hex() if invite else "none"), f"open of {digits} on {payload.hex()[:16]}...")

    # Code to link: the PS3's seal opened by Python and by the page, which finds it on the broker by the code typed
    sealed = {}
    for (_, code, invite), result in zip(seals, results[len(lines):]):
        payload = bytes.fromhex(result[3])
        sealed[code] = payload
        check(result[2] == code_topic(keys[code]), f"the PS3's topic of {code}")
        check(open_payload(keys[code], payload) == invite, f"Python opens the PS3's seal of {code}")
    check(len({payload[2:18] for payload in sealed.values()}) == len(sealed), "a new nonce for each of the PS3's seals")
    if page:
        found = run_page({"cases": [[code, invites[code].hex(), sealed[code].hex()] for code in codes],
                          "find": [[code, code_topic(keys[code]), sealed[code].hex(), f"{code[:4]} {code[4:]}"]
                                   for code in codes]})
        for code, result, link in zip(codes, found["cases"], found["found"]):
            check(result["opened"] == invites[code].hex(), f"the page opens the PS3's seal of {code}")
            check(link == f"halo://join/{invites[code].hex()}", f"Code to link finds {code}")
        check(found["missing"] is None, "Code to link: no game for a code no broker has")
        check(found["badCode"] == "A code has 8 digits.", "Code to link refuses a short code")

    times = sorted(time for time in times if time)
    print(f"key on this host: {times[len(times) // 2] if times else 0} ms (100000 PBKDF2 rounds)")
    for failure in failures:
        print(f"FAILED: {failure}")
    print(f"{checks - len(failures)} of {checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
