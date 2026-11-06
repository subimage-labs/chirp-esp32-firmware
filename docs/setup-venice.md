# Venice setup

Venice is the cloud route. chirp talks directly to the hosted chat, speech-to-text and voice models of Venice over HTTPS. No computer must stay on. Nothing runs on your machine.

## What you need

- A Venice account with API credit.
- An API key from the [API settings page](https://docs.venice.ai/guides/getting-started/generating-api-key) of Venice (`venice.ai/settings/api`). Make a key for this device only. Then you can revoke it alone.
- A device that is [flashed](flashing.md).
- A 2.4 GHz WiFi network with working internet.

The device sends your voice and text to Venice over HTTPS. The device sends the Venice key only over https. See [Security notes](security.md).

## Set up the device

1. Follow [First setup](first-setup.md) up to the **Backend** choice.
2. Choose **Venice**.
3. Paste the key into *Venice API key*. Tick *Show password and key* to check your input.
4. Do not change *Venice host*. The Hermes fields disappear. You need nothing else.
5. Tap **Save and restart**.

The setup page refuses a Venice host that starts with `http://`. The device sends the key over https only.

## What to expect

- Cost: One Venice answer makes three separate calls: listening, thinking and speaking. Venice charges for each call.
- Persona: The persona is fixed. It is *Chirper*, a friendly companion for children. It is built into the firmware.
- Voice: In the **Setup** tab, press the **Voice** button to change the voice. The button steps through Sky, Heart and Bella. chirp says "Hi, I'm Chirp!" in the new voice. The device saves the choice.
- Chats: The Chats tab offers only *+ New chat*. Venice keeps no chats. The device keeps the last 12 exchanges of the current chat in memory. A new chat or a restart empties them.
- Updates: A Venice device updates over USB. See [Updating](updating.md).

## Venice problems

| Message | Fix |
|---|---|
| *Bad API key* | The key is wrong or revoked. Enter the key again on the setup page. A blank key box keeps the stored key. |
| *Out of credit* | Add credit to your Venice account. |
| *Key not allowed* | The key has an IP allow-list that excludes your network. Change the list in the API settings of Venice. |
| *Rate limited* | Wait a moment. Then ask again. |
| *Backend unreachable* or *Backend not answering* | There is no internet, or Venice is down. |
| *Venice error* | Venice sent an unexpected error. Try again later. |

For other problems, see [Troubleshooting](troubleshooting.md).
