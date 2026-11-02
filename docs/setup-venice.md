# Venice setup

Venice is the cloud route: chirp talks straight to Venice's hosted chat,
speech-to-text and voice models over HTTPS. No computer has to stay on, and
nothing runs on your machine.

## What you need

- A Venice account with API credit.
- An API key made on Venice's
  [API settings page](https://docs.venice.ai/guides/getting-started/generating-api-key)
  (`venice.ai/settings/api`). Make one just for this device so you can revoke
  it alone.
- A device that is [flashed](flashing.md), and a 2.4 GHz WiFi network with
  working internet.

The device sends your voice and text to Venice over HTTPS. See
[Security notes](security.md).

## Set it up

1. Follow [First setup](first-setup.md) up to the **Backend** choice.
2. Choose **Venice** and paste the key into *Venice API key* (tick *Show
   password and key* to check what you typed).
3. Leave *Venice host* alone. The Hermes fields disappear; nothing else is
   needed.
4. Tap **Save and restart**.

## What to expect

- **Cost.** A Venice answer is three separate calls (listening, thinking,
  speaking), each charged by Venice.
- **Persona.** Fixed: *Chirper*, a friendly companion for kids (built into the
  firmware).
- **Voice.** In the **Setup** tab, the **Voice** button steps through Sky,
  Heart and Bella; chirp says "Hi, I'm Chirp!" in the new voice and the choice
  is saved.
- **Chats.** The Chats tab only offers *+ New chat*. Venice keeps no chats on
  its side; the device remembers the last 12 exchanges of the current chat in
  memory, and a new chat or a restart empties them.
- **Updates.** Venice devices update over USB (see [Updating](updating.md)).

## Venice problems

| Message | Fix |
|---|---|
| *Bad API key* | The key is wrong or revoked: enter it again on the phone page (a blank key box keeps the stored one). |
| *Out of credit* | Add credit on your Venice account. |
| *Key not allowed* | The key has an IP allow-list that excludes your network; change it in Venice's API settings. |
| *Rate limited* | Wait a moment and ask again. |
| *Backend unreachable* / *Backend not answering* | No internet, or Venice is down. |
| *Venice error* | Venice answered with an unexpected error: try again later. |

Other problems: [Troubleshooting](troubleshooting.md).
