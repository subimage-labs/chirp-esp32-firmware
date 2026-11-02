# Hermes setup

Hermes is the route for your own agent on your own machine. The device talks
only to your network, over plain HTTP. Listening (whisper) and the voice
(Paradee) run locally on the Hermes machine, and the answer comes from your
[Hermes Agent](https://hermes-agent.nousresearch.com/docs/getting-started/installation)
profile, with its persona (`SOUL.md`), tools and memory.

`host/pair.py`, `host/provision.py` and the speech-server installer are
Linux-only. Without Linux, use [Venice](setup-venice.md), or run Hermes on a
Linux machine.

## What you need

A Linux machine on the same network as the device, with:

- Hermes Agent [installed](https://hermes-agent.nousresearch.com/docs/getting-started/installation)
  and a model provider configured (`hermes setup`);
- Hermes' [API server enabled](https://hermes-agent.nousresearch.com/docs/user-guide/features/api-server)
  (`API_SERVER_ENABLED=true` in `~/.hermes/.env`) and the gateway running
  (`hermes gateway start`). `pair.py` also needs
  `hermes config set gateway.multiplex_profiles true` and tells you if it is
  off;
- `systemd` user services (`pair.py` and the speech-server installer use
  `systemctl --user`), Python 3.10–3.12 and room for two small local speech
  models (`pair.py` says what it downloads before it does);
- the ports `8642` (Hermes) and `8643` (chirp's speech server) reachable from
  the device's WiFi, and ideally a fixed address for the machine (a DHCP
  reservation): the device stores that address.

Clone this repository on the Hermes machine (see [Flashing](flashing.md)) and
flash the device first.

## Pair the device

With the device plugged into the Hermes machine:

```sh
python3 host/pair.py                 # device plugged into this machine (recommended)
python3 host/pair.py --no-usb        # prints a QR code instead; scan it with the phone
                                     #   while the phone is on the Chirp-XXXX network
```

`pair.py` asks before changing anything. It walks through:

1. Checks that Hermes and its gateway are running.
2. Asks which Hermes profile this agent is — link an existing one, or create a
   new one:
   - **Link** (`--link NAME`, `default` for the root profile): the profile
     keeps its persona, model and history, and chirp joins its conversations.
   - **Create** (`--create NAME`): fresh with your current model, or a copy of
     another profile's setup (`--clone-from NAME`: config, tools, skills,
     persona — not its history). It offers a starter persona written for
     speech (short spoken answers, no lists or links).

   Either way the persona is the profile's `SOUL.md` — edit it to change who
   answers. After the persona is settled, `pair.py` proposes a **character
   look** for the agent (see below).
3. Gives the profile an API key: a linked profile keeps its own; a new one
   always gets a fresh key, even when copied from another profile.
4. Makes sure the speech server runs: whisper `base` for listening and Paradee
   for the voice, both local. It explains and asks before installing. (You can
   also install it on its own: `host/setup.sh --install-service`, then
   `curl http://localhost:8643/health`.)
5. Checks the gateway answers on your LAN address. If it only listens on the
   machine itself, it explains what opening it means (every profile's API
   becomes reachable on your network, each behind its own key) and asks.
6. Sends the agent to the device over USB and waits for the device's own check
   to report ready. The first time you also type your WiFi name and password;
   later runs can leave the WiFi blank (the device keeps its saved networks) or
   add another one.

The USB handoff is the recommended one: no key crosses WiFi or a screen.

### Without USB: a QR code

`python3 host/pair.py --no-usb` shows a QR code instead (it needs `qrencode`;
without it the setup URL is written to a private file under `~/.config/chirp/`
and only the path is printed). Join the `Chirp-XXXX` network shown on the
device with your phone, scan the code, and pick your home WiFi on the page that
opens. The code carries the profile's API key, so don't share it or screenshot
it. The pairing code sits in the URL fragment (`#pair=`): it never reaches the
device as a request and the page removes it from the address bar after reading
it.

## Character look

In character mode each agent is drawn as a pixel-art character (see
[Using chirp](using.md)). By default it comes from the profile name alone.
`pair.py` can refine it from the persona: it matches the profile name and
`SOUL.md` against a short keyword table (`host/avatar_look.py`, one line per
rule: kitchen → toque and whisk, salon → scissors and bow, code → headphones
and glasses, and so on), describes the result in a sentence ("a Tangerine
character with no ears, wearing a toque and holding a whisk, bouncy"), and
asks:

```
Use this look? [a]ccept / [r]eroll / [k]eep the default:
```

*Reroll* keeps what the persona decided and redraws the rest (the same number
always gives the same look); *keep* clears any look the device had. The text of
`SOUL.md` stays on this machine: only twelve trait numbers are sent (in the
`look` member of the serial settings line or the pairing QR code, hex, one byte
per trait, `ff` = keep the seeded one) and the device stores them with the
agent's key. `--look` accepts the proposal without asking, `--look
hat=toque,palette=Teal` edits it first, `--look none` clears it; with `--yes`
and no `--look` the device's character is left alone.

On the device, *Original* in the Chirper tab is this look (or the seeded
character when there is none). A character you keep in the Chirper tab wins
over it, for that agent.

## More than one agent

A device holds up to 4 agents (Hermes profiles, each with its own key). Every
`pair.py` run adds one, or replaces the key of one it already has, and makes it
the one in use; the others stay. Switch on the device: open the **Setup** tab
(five side-key taps; T-Deck: five ball clicks), select *Switch to NAME* (dial
or ball) and press the dial key (T-Deck: Enter); the device saves and restarts
in about 6 s. Each agent resumes its own last conversation.

Remove one with `python3 host/pair.py --forget NAME` (USB) or its *remove* box
on the phone setup page; the profile itself stays on Hermes. All agents live on
one Hermes host: pointing the device at a different host (including a
`pair.py` run from another machine, or the same machine under another name or
address) replaces them with the agent added in that step, so a key never
reaches a gateway that didn't issue it.

## Hermes problems

| Message | Fix |
|---|---|
| Hermes key rejected | Wrong API key for the agent: run `host/pair.py` again, or Setup tab, *Set up on phone*. |
| Hermes profile not found | The profile name doesn't exist on the gateway. |
| Hermes unreachable | The gateway isn't running or not reachable on the LAN (`host/pair.py` checks). |
| Speech server unreachable | The speech server isn't running: `systemctl --user status hermes-audio-shim`. |

A changed address for the Hermes machine needs a re-pair. More:
[Troubleshooting](troubleshooting.md).
