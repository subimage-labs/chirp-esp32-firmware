# Hermes setup

Hermes is the route for your own agent on your own machine. The device talks only to your network, over plain HTTP. The Hermes machine does the listening (whisper) and the voice (Paradee) locally. The answer comes from your [Hermes Agent](https://hermes-agent.nousresearch.com/docs/getting-started/installation) profile. The answer uses the persona (`SOUL.md`), the tools and the memory of the profile.

`host/pair.py`, `host/provision.py` and the speech-server installer run on Linux only. Without Linux, use [Venice](setup-venice.md), or run Hermes on a Linux machine.

## What you need

A Linux machine on the same network as the device. The machine needs these items:

- Hermes Agent [installed](https://hermes-agent.nousresearch.com/docs/getting-started/installation), and a model provider configured (`hermes setup`).
- The Hermes [API server enabled](https://hermes-agent.nousresearch.com/docs/user-guide/features/api-server). Set `API_SERVER_ENABLED=true` in `~/.hermes/.env`. Start the gateway with `hermes gateway start`.
- The setting `hermes config set gateway.multiplex_profiles true`. `pair.py` needs it and tells you if it is off.
- `systemd` user services. `pair.py` and the speech-server installer use `systemctl --user`.
- Python 3.10 to 3.12.
- Room for two small local speech models. `pair.py` tells you what it downloads before it downloads.
- The ports `8642` (Hermes) and `8643` (the speech server of chirp), reachable from the WiFi of the device.
- A fixed address for the machine, for example a DHCP reservation. The device stores this address.

Clone this repository on the Hermes machine (see [Flashing](flashing.md)). Flash the device first.

## Pair the device

Plug the device into the Hermes machine. Then run one of these commands:

```sh
python3 host/pair.py                 # device plugged into this machine (recommended)
python3 host/pair.py --no-usb        # prints a QR code instead; scan it with the phone
                                     #   while the phone is on the Chirp-XXXX network
```

`pair.py` asks before it changes anything. It does these steps:

1. It checks that Hermes and its gateway run.
2. It asks which Hermes profile this agent is. You can link an existing profile or create a new one.
   - **Link** (`--link NAME`; use `default` for the root profile): The profile keeps its persona, its model and its history. chirp joins its conversations.
   - **Create** (`--create NAME`): The new profile starts fresh with your current model. Or it copies the setup of another profile (`--clone-from NAME`). The copy has the config, the tools, the skills and the persona. It does not have the history. `pair.py` also offers a starter persona written for speech. The persona gives short spoken answers, with no lists and no links.
3. It gives the profile an API key. A linked profile keeps its own key. A new profile always gets a new key, also when you copy it from another profile.
4. It makes sure that the speech server runs. The server uses whisper `base` for listening and Paradee for the voice. Both run locally. `pair.py` explains the install and asks before it installs. To install the server alone, run `host/setup.sh --install-service`. Then run `curl http://localhost:8643/health`.
5. It checks that the gateway answers on your LAN address. If the gateway listens only on the machine itself, `pair.py` explains what an open gateway means. Every profile API becomes reachable on your network, each behind its own key. Then it asks.
6. It sends the agent to the device over USB. Then it waits until the self-check of the device reports ready. The first time, you type your WiFi name and password. Later runs can leave the WiFi blank, because the device keeps its saved networks. Or you can add another network.

In both cases, the persona is the `SOUL.md` of the profile. Edit this file to change who answers. After the persona is settled, `pair.py` proposes a **character look** for the agent (see below).

Use the USB handoff when you can. No key crosses the WiFi or a screen.

### Without USB: a QR code

Run `python3 host/pair.py --no-usb`. The script shows a QR code. It needs `qrencode`. Without `qrencode`, the script writes the setup URL to a private file under `~/.config/chirp/` and prints only the path.

1. On the phone, join the `Chirp-XXXX` network that the device shows.
2. Scan the code.
3. On the page that opens, pick your home WiFi.

The code carries the API key of the profile. Do not share it and do not take a screenshot of it. The pairing code is in the URL fragment (`#pair=`). It never reaches the device as a request. The page removes it from the address bar after it reads it.

## Character look

In character mode, each agent is drawn as a pixel-art character (see [Using chirp](using.md)). By default, the character comes from the profile name alone. `pair.py` can refine it from the persona.

`pair.py` matches the profile name and `SOUL.md` against a short keyword table (`host/avatar_look.py`). The table has one line for each rule. Examples: kitchen gives a toque and a whisk, salon gives scissors and a bow, code gives headphones and glasses. `pair.py` describes the result in one sentence. For example: "a Tangerine character with no ears, wearing a toque and holding a whisk, bouncy". Then it asks:

```
Use this look? [a]ccept / [r]eroll / [k]eep the default:
```

- *Reroll* keeps what the persona decided and draws the rest again. The same number always gives the same look.
- *Keep* clears any look that the device had.

The text of `SOUL.md` stays on this machine. The device receives only twelve trait numbers. They are in the `look` member of the serial settings line or the pairing QR code. They are in hex, one byte for each trait. The value `ff` means: keep the seeded trait. The device stores the numbers with the key of the agent.

- `--look` accepts the proposal without a question.
- `--look hat=toque,palette=Teal` edits the proposal first.
- `--look none` clears the look.
- With `--yes` and no `--look`, the character of the device does not change.

On the device, *Original* in the Chirper tab is this look. If the agent has no look, *Original* is the seeded character. A character that you keep in the Chirper tab wins over the look for that agent.

## More than one agent

A device holds up to 4 agents. Each agent is a Hermes profile with its own key. Each `pair.py` run adds one agent, or replaces the key of an agent that the device already has. The new agent becomes the agent in use. The other agents stay.

To switch the agent on the device:

1. Open the **Setup** tab. Tap the side key five times (T-Deck: click the ball five times).
2. Select *Switch to NAME*. Use the dial or the ball.
3. Press the dial key (T-Deck: Enter).

The device saves the choice and restarts in about 6 s. Each agent resumes its own last conversation.

To remove an agent, use one of these methods:

- Run `python3 host/pair.py --forget NAME` (USB).
- Tick the *remove* box on the setup page.

The profile stays on Hermes.

All agents live on one Hermes host. You can point the device at a different host. For example, you can run `pair.py` from another machine, or use the same machine under another name or address. Then the new agent replaces all other agents. This way, a key never reaches a gateway that did not issue it.

## Hermes problems

| Message | Fix |
|---|---|
| Hermes key rejected | The API key of the agent is wrong. Run `host/pair.py` again, or open the Setup tab and pick *Set up on phone*. |
| Hermes profile not found | The profile name does not exist on the gateway. |
| Hermes unreachable | The gateway does not run, or the LAN cannot reach it. `host/pair.py` checks this. |
| Speech server unreachable | The speech server does not run. Run `systemctl --user status hermes-audio-shim`. |

If the address of the Hermes machine changes, pair the device again. For more help, see [Troubleshooting](troubleshooting.md).
