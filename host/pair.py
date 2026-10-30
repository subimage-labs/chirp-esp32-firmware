#!/usr/bin/env python3
"""Pair a Chirp voice device with the Hermes gateway on this machine.

Runs the host half of adding an agent to a device: checks the gateway, picks
or creates a Hermes profile, gives it an API key, makes sure the gateway and
the speech server are reachable, verifies the device's API path, then hands
the device the agent — over USB when it is plugged in, else as a QR code the
phone scans on the device's setup screen. A device holds up to 4 agents;
each run adds one (or replaces its key) and makes it the one in use. Switch
between them on the device: side key three times (Setup tab), wheel, dial key.

Never prints the API key, the WiFi password or the QR/URL fallback's URL (the
URL carries the key: it goes to qrencode on stdin, or, without qrencode, into
a 0600 file under ~/.config/chirp/ whose path is all that is printed). The
key travels over plain HTTP on the LAN; the last line of a run says so.

    host/pair.py [--link NAME | --create NAME [--clone-from NAME]]
                 [--host ADDR] [--port /dev/ttyACMx] [--no-usb] [--yes]
                 [--look [SPEC]]
    host/pair.py --forget NAME [--port /dev/ttyACMx]

--link NAME        Link an existing Hermes profile ('default' = the root
                   profile). Its persona, model and history stay as they are.
--create NAME      Create a new profile just for Chirp (fresh: your current
                   model), with an optional starter spoken persona.
--clone-from NAME  With --create: start from a copy of that profile's config,
                   persona and skills (not its history or messaging channels).
                   The new profile still gets its own API key.
                   Without --link or --create, pair.py asks which.
--forget NAME      Remove that agent from the device over USB. The profile
                   and its key stay on Hermes. (The phone setup form can
                   remove agents too.)
--host ADDR        Address the device contacts this machine on (default: the
                   address of the default route, confirmed interactively).
--port PATH        USB serial port for the wired handoff (default: first
                   /dev/ttyACM*).
--no-usb           Skip the wired handoff; always print the pairing QR.
--look [SPEC]      Give the agent a character look derived from its persona
                   (SOUL.md, read here, never printed or sent: only trait
                   numbers go to the device). Without it, pair.py proposes the
                   look and asks (accept / reroll / keep the default); with
                   --yes it keeps the device's character unless --look is
                   given. SPEC edits the proposal ("hat=toque,palette=Teal";
                   "none" clears the look). Rules: host/avatar_look.py.
--yes              Auto-answer the non-destructive questions (starter persona,
                   use the detected host address). Opening the gateway to the
                   LAN, restarting it, installing the speech service, and
                   writing to an attached device always ask.

Environment: WIFI_SSID / WIFI_PASS add a WiFi network to the device for the wired
handoff (for unattended runs; otherwise asked interactively; a blank SSID adds
none: the device keeps the networks it has, up to 5, and joins the strongest in
range; a name it already holds is edited).
"""

import argparse
import base64
import getpass
import json
import os
import secrets
import shutil
import socket
import subprocess
import re
import sys
import time
import urllib.error
import urllib.request

import avatar_look
import provision

HOST_DIR = os.path.dirname(os.path.abspath(__file__))
GATEWAY_SERVICE = "hermes-gateway"
HERMES_PORT = 8642
SHIM_HEALTH = "http://127.0.0.1:8643/health"
REACH_TIMEOUT_S = 2.0
KEY_MIN = 16
SELFTEST_S = 40.0
# Hermes profile IDs, capped at the device's 32-character profile field.
NEW_PROFILE_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")
NEW_PROFILE_DEFAULT = "chirp"
# The device's setup soft AP serves the portal here (firmware's soft-AP
# address). Assembled from octets so no private dotted literal is tracked.
SOFT_AP = ".".join(str(o) for o in (192, 168, 4, 1))

STARTER_SOUL = (
    "You are {name}, the voice assistant inside a small device called Chirp.\n"
    "Keep every answer short: one to three spoken sentences, plain words, no lists, no symbols, no web addresses.\n"
    "When asked about volume, say to hold the side button and turn the dial.\n"
)


def stop(msg):
    sys.exit("pair: " + msg)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


# Always name the profile: bare `hermes` follows a sticky `hermes profile use`
# choice, while the gateway's own settings live in the default profile.
def hermes(profile, *args):
    return run(["hermes", "-p", profile or "default"] + list(args))


def ask(prompt, default=False, auto=False):
    if auto:
        return default
    suffix = " [Y/n] " if default else " [y/N] "
    ans = input(prompt + suffix).strip().lower()
    return (not ans and default) or ans in ("y", "yes")


# ---------------------------------------------------------------- preflight

def preflight():
    if not shutil.which("hermes"):
        stop("the hermes CLI is not on PATH; install Hermes on this machine first")
    active = run(["systemctl", "--user", "is-active", GATEWAY_SERVICE])
    if active.stdout.strip() != "active":
        status = run(["hermes", "gateway", "status"])
        if "active (running)" not in status.stdout:
            stop("the gateway is not running; start it with: hermes gateway start")
    val = hermes("", "config", "get", "gateway.multiplex_profiles")
    if val.stdout.strip() != "true":
        stop("gateway.multiplex_profiles is off. Chirp uses its own profile, so the "
             "gateway must serve profiles side by side: run "
             "`hermes config set gateway.multiplex_profiles true` and restart the "
             "gateway, then re-run pair.py")
    print("gateway: running, profile multiplexing on")


# ---------------------------------------------------------------- profile

def hermes_home():
    return os.environ.get("HERMES_HOME") or os.path.expanduser("~/.hermes")


def list_profiles():
    """Canonical profile IDs: "" (the default profile) plus every named profile
    directory. The `hermes profile list` table shows display names instead."""
    root = os.path.join(hermes_home(), "profiles")
    try:
        names = sorted(d for d in os.listdir(root)
                       if os.path.isfile(os.path.join(root, d, "config.yaml")))
    except OSError:
        names = []
    return [""] + names


def profile_label(name):
    return name or "default"


def link_profile(profiles, name):
    wanted = "" if name == "default" else name     # Hermes' name for the root profile
    if wanted not in profiles:
        stop(f"no Hermes profile named '{name}'. Existing: "
             + ", ".join(profile_label(p) for p in profiles)
             + f". To make a new one: host/pair.py --create {name}")
    print(f"profile: linking the existing '{profile_label(wanted)}' profile "
          "(its persona, model and history stay as they are)")
    return wanted


def pick_number(prompt, count):
    while True:
        pick = input(f"{prompt} [1-{count}]: ").strip()
        if pick.isdigit() and 1 <= int(pick) <= count:
            return int(pick)


def valid_new_name(name, profiles):
    if not NEW_PROFILE_RE.match(name):
        return "use lowercase letters, digits, '-' or '_' (up to 32, starting with a letter or digit)"
    if name in profiles or name == "default":
        return f"'{name}' already exists; link it instead"
    return None


def create_profile(profiles, name, clone_from):
    """`hermes profile create`, fresh or copying another profile's setup. A copy
    takes its config, persona, skills and .env (secrets included) but not its
    history or messaging channels; the device still gets its own new key."""
    problem = valid_new_name(name, profiles)
    if problem:
        stop(f"can't create '{name}': {problem}")
    cmd = ["hermes", "profile", "create", name]
    if clone_from is not None:
        if clone_from not in profiles:
            stop(f"no Hermes profile named '{profile_label(clone_from)}' to copy from")
        cmd += ["--clone-from", profile_label(clone_from)]
    made = run(cmd)
    if made.returncode != 0:
        stop("creating the profile failed: " + (made.stderr.strip() or made.stdout.strip()
                                                or "unknown error"))
    source = f"a copy of '{profile_label(clone_from)}'" if clone_from is not None else \
        "fresh, using your current model"
    print(f"profile: created '{name}' ({source})")
    return name


def choose_profile(args):
    """-> (profile, created). "" is the default profile."""
    profiles = list_profiles()
    if args.link:
        return link_profile(profiles, args.link), False
    if args.create:
        clone = None if args.clone_from is None else \
            ("" if args.clone_from == "default" else args.clone_from)
        return create_profile(profiles, args.create, clone), True

    print("Chirp answers as a Hermes profile (the device can hold up to 4). Either:")
    print("  1. link an existing profile (it keeps its persona, model and history)")
    print("  2. create a new profile just for Chirp")
    if pick_number("Choice", 2) == 1:
        for i, name in enumerate(profiles, 1):
            print(f"  {i}. {profile_label(name)}")
        return link_profile(profiles, profile_label(
            profiles[pick_number("Profile to link", len(profiles)) - 1])), False

    while True:
        name = input(f"New profile name [{NEW_PROFILE_DEFAULT}]: ").strip() or NEW_PROFILE_DEFAULT
        problem = valid_new_name(name, profiles)
        if not problem:
            break
        print("  " + problem)
    print("Start it from:")
    print("  1. a fresh profile (uses your current model)")
    for i, p in enumerate(profiles, 2):
        print(f"  {i}. a copy of '{profile_label(p)}' (model, tools, skills, persona; "
              "not its history)")
    n = pick_number("Start from", len(profiles) + 1)
    return create_profile(profiles, name, None if n == 1 else profiles[n - 2]), True


def offer_soul(env_path, profile, created, auto):
    if not created:
        return                      # a linked profile's SOUL.md is the owner's
    path = os.path.join(os.path.dirname(env_path), "SOUL.md")
    copied = os.path.exists(path) and os.path.getsize(path) > 0
    prompt = ("Replace the copied SOUL.md with a starter spoken persona" if copied else
              "Write a starter SOUL.md (spoken persona) for the new profile")
    if not ask(prompt, default=not copied, auto=auto):
        return
    who = input(f"Assistant name [{profile}]: ").strip() or profile
    with open(path, "w") as f:
        f.write(STARTER_SOUL.format(name=who))
    print(f"soul: wrote {path} (edit any time; Hermes reloads it per message)")


# ---------------------------------------------------------------- api key

def read_env_value(path, name):
    try:
        with open(path) as f:
            lines = f.readlines()
    except OSError:
        return None
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export "):].lstrip()
        k, sep, v = line.partition("=")
        if not sep or k.strip() != name:
            continue
        v = v.strip()
        if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
            v = v[1:-1]
        elif " #" in v:
            v = v.split(" #", 1)[0].rstrip()
        return v
    return None


def profile_env_path(profile):
    r = hermes(profile, "config", "env-path")
    path = r.stdout.strip()
    if r.returncode != 0 or not path:
        stop("could not locate the profile's .env (`hermes config env-path` failed)")
    return path


# A new profile always gets its own key: a profile copied with --clone-from
# arrives holding the source profile's key.
def ensure_api_key(profile, env_path, fresh):
    api_key = read_env_value(env_path, "API_SERVER_KEY")
    if not fresh and api_key and len(api_key) >= KEY_MIN:
        print(f"api key: using the one already in {env_path}")
        return api_key
    api_key = secrets.token_urlsafe(32)
    r = hermes(profile, "config", "set", "API_SERVER_KEY", api_key)
    if r.returncode != 0 or "✓" not in r.stdout:
        redacted = (r.stderr or r.stdout or "unknown error").replace(api_key, "***")
        stop("writing API_SERVER_KEY failed: " + " ".join(redacted.split()))
    print(f"api key: generated ({len(api_key)} chars), stored in {env_path}")
    return api_key


# ---------------------------------------------------------------- reachability

def reachable(host, port):
    try:
        with socket.create_connection((host, port), timeout=REACH_TIMEOUT_S):
            return True
    except OSError:
        return False


# The bind can come from config.yaml, ~/.hermes/.env or the service unit, so
# the truth is whether the gateway answers on the address the device will use.
def gateway_on_lan(host):
    if reachable(host, HERMES_PORT):
        print(f"gateway: answers on {host}:{HERMES_PORT} — reachable from the LAN")
        return True
    print(f"gateway: nothing answers on {host}:{HERMES_PORT}; the API listens on loopback")
    print("only (or a firewall blocks it), and Chirp reaches it over WiFi.")
    print("Opening it to the LAN exposes EVERY profile's API on this network; each is")
    print("protected by its own API key, but anyone on the LAN can then try them.")
    if not ask("Open the gateway to the LAN now"):
        print("pair: leaving it as is — the device will report "
              "'Backend unreachable' until it is opened")
        return False
    r = hermes("", "config", "set", "platforms.api_server.host", "0.0.0.0")
    if r.returncode != 0:
        stop("setting platforms.api_server.host failed: " + (r.stderr.strip() or "unknown error"))
    if not ask("Restart the gateway now to apply it (interrupts running conversations)",
               default=True):
        print("pair: apply it later with: systemctl --user restart " + GATEWAY_SERVICE)
        return False
    run(["systemctl", "--user", "restart", GATEWAY_SERVICE])
    for _ in range(30):
        time.sleep(1)
        if reachable(host, HERMES_PORT):
            print(f"gateway: restarted, answering on {host}:{HERMES_PORT}")
            return True
    print(f"pair: the gateway still doesn't answer on {host}:{HERMES_PORT} — check a firewall")
    return False


def shim_healthy():
    try:
        with urllib.request.urlopen(SHIM_HEALTH, timeout=3) as r:
            return r.status == 200
    except OSError:
        return False


def ensure_speech():
    if shim_healthy():
        print("speech: local server healthy (whisper-base STT + Paradee TTS, port 8643)")
        return
    print("speech: no speech server on port 8643. host/setup.sh --install-service would")
    print("create a Python venv under host/.venv-tts, install faster-whisper and Paradee,")
    print("download the whisper 'base' speech-to-text model and the 8M-parameter Paradee")
    print("text-to-speech voice (all run locally, nothing goes to a cloud), then install")
    print("and start the hermes-audio-shim service.")
    if not ask("Run host/setup.sh --install-service now", default=True):
        print("pair: the device will report 'Speech server unreachable' until it runs")
        return
    r = subprocess.run([os.path.join(HOST_DIR, "setup.sh"), "--install-service"])
    if r.returncode != 0 or not shim_healthy():
        print("pair: speech service still not healthy; check: "
              "systemctl --user status hermes-audio-shim")
        return
    print("speech: local server healthy")


def lan_address():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))     # route lookup only — no packet is sent
            return s.getsockname()[0]
        finally:
            s.close()
    except OSError:
        return None


def host_address(args):
    if args.host:
        return args.host
    detected = lan_address()
    if not detected:
        return input("Could not detect this machine's LAN address. Enter it: ").strip()
    if ask(f"Use {detected} as this machine's address", default=True, auto=args.yes):
        return detected
    return input("Address for the device to use: ").strip()


def api_sessions_url(host, profile):
    base = f"http://{host}:{HERMES_PORT}" + (f"/p/{profile}" if profile else "")
    return base + "/api/sessions"


def verify(host, on_lan, profile, api_key):
    target = host if on_lan else "127.0.0.1"
    url = api_sessions_url(target, profile)
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + api_key})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            status = r.status
    except urllib.error.HTTPError as e:
        status = e.code
    except OSError as e:
        print(f"verify: cannot reach the gateway ({e}). It is not listening on "
              f"{target}:{HERMES_PORT} — check the bind and any firewall.")
        return False
    if status == 200:
        print("verify: the gateway answered the profile's API (200) — the device's path works")
        return True
    if status == 401:
        print("verify: the key was rejected (401). The profile's API_SERVER_KEY no longer "
              "matches; re-run pair.py or set it with "
              f"`hermes -p {profile or 'default'} config set API_SERVER_KEY <key>`")
    elif status == 404:
        print("verify: that profile is not served (404). Check `hermes profile list` and "
              "`hermes config get gateway.multiplex_profiles`")
    else:
        print(f"verify: unexpected status {status} from the gateway")
    return False


# ---------------------------------------------------------------- handoff

def wait_selftest(port=None, timeout=SELFTEST_S):
    """Wait for the device's post-restart `[SelfTest] <result>` serial line."""
    from_log = provision.watcher_running() and os.path.exists(provision.LOG)
    pos = os.path.getsize(provision.LOG) if from_log else 0
    fd = -1
    seen = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.2)
        if from_log:
            with open(provision.LOG, "rb") as f:
                f.seek(pos)
                seen += f.read()
                pos = f.tell()
        else:
            if fd < 0:                     # the port re-enumerates when the device restarts
                p = provision.pick_port(port)
                if p:
                    try:
                        fd = os.open(p, os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
                    except OSError:
                        pass
            if fd >= 0:
                try:
                    seen += os.read(fd, 4096)
                except (BlockingIOError, OSError):
                    pass
                try:
                    os.close(fd)
                    fd = -1
                except OSError:
                    pass
        for line in seen.decode(errors="replace").splitlines():
            if line.startswith("[SelfTest]"):
                return line.strip()
    return None


def handoff_usb(args, host, profile, api_key, look=None):
    port = provision.pick_port(args.port)
    if not ask(f"Device found on {port}. Add '{profile_label(profile)}' to it and use it"):
        return
    settings = {"backend": "local", "hermes": host, "key": api_key, "profile": profile,
                "shim": ""}
    if look is not None:
        settings["look"] = look
    ssid = os.environ.get("WIFI_SSID")
    if ssid is None:
        ssid = input("WiFi network (SSID) for the device "
                     "(blank = add none, keep the ones it has): ").strip()
    if ssid:
        password = os.environ.get("WIFI_PASS")
        if password is None:
            password = getpass.getpass("WiFi password (blank for an open network): ")
        settings.update(ssid=ssid, **{"pass": password})
    try:
        reply = provision.send_settings(settings, args.port)
    except (ValueError, RuntimeError) as exc:
        stop(str(exc))
    print(reply)
    if not reply.startswith("[Settings] saved"):
        return
    print("waiting for the device to restart and run its self-test…")
    line = wait_selftest(args.port)
    if not line:
        print(f"pair: no [SelfTest] line within {SELFTEST_S:.0f} s; "
              f"check the serial log: {provision.LOG}")
        return
    print(line)
    print("device ready" if line == "[SelfTest] ok" else
          "pair: the device's self-test failed — see its message above")


def forget_agent(args):
    """Remove one agent from the device over USB. Hermes is not touched: the
    profile and its key stay on the gateway."""
    name = "" if args.forget == "default" else args.forget
    port = provision.pick_port(args.port)
    if not port:
        stop("--forget needs the device on USB (or remove it on the phone setup form)")
    try:
        reply = provision.send_settings({"forget": name}, port)
    except RuntimeError as exc:
        stop(str(exc))
    print(reply)
    if not reply.startswith("[Settings] saved"):
        sys.exit(1)


def pair_code(host, profile, api_key, look=None):
    members = {"hermes": host, "profile": profile, "shim": "", "key": api_key}
    if look is not None:
        members["look"] = look
    payload = json.dumps(members, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(payload).rstrip(b"=").decode()


def pairing_url(host, profile, api_key, look=None):
    """The portal URL the device's setup QR code opens. The pairing code rides
    in the fragment: it never goes to the device in a request line and never
    into a Referer; the page's script moves it into the form and strips it."""
    return f"http://{SOFT_AP}/#pair={pair_code(host, profile, api_key, look)}"


PAIR_URL_FILE = "pairing-url.txt"


def write_pairing_url(url):
    """The URL in a 0600 file under ~/.config/chirp/ (the key is in it); -> path."""
    d = os.path.join(os.path.expanduser("~"), ".config", "chirp")
    os.makedirs(d, mode=0o700, exist_ok=True)
    path = os.path.join(d, PAIR_URL_FILE)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.fchmod(fd, 0o600)                       # an older, looser file keeps its mode otherwise
        os.write(fd, url.encode() + b"\n")
    finally:
        os.close(fd)
    return path


def handoff_qr(host, profile, api_key, look=None):
    """Show the pairing QR code. The URL carries the API key, so it is never
    printed and never on a command line (argv is readable by every local user):
    qrencode gets it on stdin."""
    url = pairing_url(host, profile, api_key, look)
    print("handoff: the USB handoff (device plugged in) is the safer path: no key")
    print("crosses a WiFi network or a screen. Use this QR code only without a cable.")
    print("1. On its screen the device shows a WiFi network named Chirp-XXXX with a")
    print("   password — join that network on the phone.")
    print("2. The code below opens the device's setup page. It carries the profile's")
    print("   API key, so do not screenshot or share it.")
    if shutil.which("qrencode"):
        subprocess.run(["qrencode", "-t", "ANSIUTF8", "-o", "-"], input=url.encode())
        return
    path = write_pairing_url(url)
    print("pair: qrencode is not installed, so there is no code to scan. The setup URL")
    print("   (it contains the API key) is in a private file; it is not printed:")
    print(f"   {path}")
    print("   Install qrencode, or open that file's URL on the phone yourself, then")
    print("   delete the file.")


def lan_note():
    """Where the key travels once paired: said on every handoff."""
    print(f"note: the device sends its key over plain HTTP to port {HERMES_PORT} on this")
    print("machine. Anyone on the LAN who can sniff that traffic can read it. Limit the")
    print("port with a firewall rule so only the device's address reaches it. (TLS with")
    print("a pinned certificate on the device would remove this; it is not built yet.)")


# ---------------------------------------------------------------- character look
# The persona (SOUL.md) is read here and matched by host/avatar_look.py; only
# the resulting trait numbers leave this machine, and the persona text is never
# printed, logged or written anywhere.

SOUL_MAX = 64 * 1024


def read_soul(env_path):
    """The profile's SOUL.md next to its .env, or "" when there is none."""
    path = os.path.join(os.path.dirname(env_path), "SOUL.md")
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read(SOUL_MAX)
    except OSError:
        return ""


def choose_look(args, profile, soul):
    """-> None (leave the device's character alone), "" (clear the look: the
    seeded default) or the look as hex. Prints only a description of traits."""
    spec = args.look
    if spec is None and args.yes:
        return None
    rec, fired = avatar_look.derive(profile, soul)
    if spec not in (None, "auto"):
        if spec.strip().lower() in ("none", "default", "clear"):
            print("look: keeping the seeded default character")
            return ""
        try:
            rec = avatar_look.apply_edits(rec, spec)
        except ValueError as exc:
            stop(str(exc))
    shown = f"look: {avatar_look.describe(rec)}"
    if fired:
        shown += f"  (from: {', '.join(fired)})"
    print(shown)
    if spec is not None:
        return avatar_look.to_hex(rec)
    n = 0
    while True:
        ans = input("Use this look? [a]ccept / [r]eroll / [k]eep the default: ").strip().lower()
        if ans in ("a", "accept", "y", "yes", ""):
            return avatar_look.to_hex(rec)
        if ans in ("k", "keep", "n", "no"):
            print("look: keeping the seeded default character")
            return ""
        if ans in ("r", "reroll"):
            n += 1
            rec = avatar_look.reroll(avatar_look.derive(profile, soul)[0], profile, n)
            print(f"look: {avatar_look.describe(rec)}  (reroll {n})")


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(
        description="Pair a Chirp voice device with the Hermes gateway on this machine.")
    which = ap.add_mutually_exclusive_group()
    which.add_argument("--link", metavar="NAME",
                       help="link an existing Hermes profile ('default' = the root profile)")
    which.add_argument("--create", metavar="NAME", help="create a new Hermes profile for Chirp")
    which.add_argument("--forget", metavar="NAME",
                       help="remove this agent from the device (USB); Hermes is unchanged")
    ap.add_argument("--clone-from", metavar="NAME",
                    help="with --create: copy this profile's setup (not its history)")
    ap.add_argument("--host", help="address the device contacts this machine on")
    ap.add_argument("--port", help="USB serial port for the wired handoff")
    ap.add_argument("--no-usb", action="store_true", help="always use the QR handoff")
    ap.add_argument("--look", nargs="?", const="auto", metavar="SPEC",
                    help="give the agent a persona-derived character look (edits: hat=toque,...)")
    ap.add_argument("--yes", action="store_true",
                    help="auto-confirm the non-destructive questions")
    args = ap.parse_args()
    if args.clone_from is not None and not args.create:
        ap.error("--clone-from needs --create")
    if args.forget is not None:
        forget_agent(args)
        return
    preflight()
    profile, created = choose_profile(args)
    env_path = profile_env_path(profile)
    offer_soul(env_path, profile, created, args.yes)
    look = choose_look(args, profile, read_soul(env_path))
    api_key = ensure_api_key(profile, env_path, fresh=created)
    ensure_speech()
    host = host_address(args)
    on_lan = gateway_on_lan(host)
    if not verify(host, on_lan, profile, api_key):
        print("pair: continuing, but the device will not work until this is fixed")

    if not args.no_usb and provision.pick_port(args.port):
        handoff_usb(args, host, profile, api_key, look)
    else:
        if not args.no_usb:
            print("handoff: no /dev/ttyACM* device found")
        handoff_qr(host, profile, api_key, look)
    lan_note()


if __name__ == "__main__":
    main()
