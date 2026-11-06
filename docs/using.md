# Using chirp

Wait for the chat screen. Hold the talk key, ask a question, and release the key. The device speaks the answer while it arrives.

## Controls

| Action | T-Embed CC1101 Plus | T-Deck |
|---|---|---|
| Talk. Hold the key and release it to ask. Press it while the device speaks to interrupt. | Hold the talk key. This is the encoder key (the dial key). | Hold the **mic key** on the keyboard. |
| Turn hands-free voice mode on or off | Double-tap the talk key. | Double-tap the mic key. |
| Menu: open, go to the next tab, go back to the chat | Tap the side key. Five taps reach **Setup**. A tap while the device thinks stops the turn. | Click the trackball. Five clicks reach **Setup**. **Backspace** leaves the menu. |
| Select, scroll | Turn the dial. | Roll the ball up or down. A sideways roll does nothing. |
| Activate the selected row | Press the talk key. | Press **Enter**. |
| Volume (0-21, where 21 is unity gain) | Hold the side key for 1.5 s, then turn the dial. | Hold the ball click, then roll the ball. |
| Switch between the character view and the chat view | Hold the talk key and tap the side key. | Hold the mic key and click the ball. |
| Type a question | Not available. | Type the question and press **Enter** to send it. |

The rest of this page uses the names of the T-Embed controls. On the T-Deck, use the mic key for the talk key, the ball click for the side key, and a roll of the ball for a turn of the dial.

### Details

| Input | Action |
|---|---|
| Talk key (hold) | Talk. Release the key to ask. Press the key while the device speaks to interrupt it and ask again. The mic starts when you press the key. The screen shows Listening only after you hold the key for 0.5 s, so a short tap does not flash the screen. The device never sends a press shorter than 0.3 s as speech. |
| Talk key (double-tap) | Turn hands-free voice mode on or off. The second press must come within 0.4 s. A turn opens when you speak and closes after 2 s of silence. Voice mode ends after 3 minutes of quiet. |
| Talk key held, then side key tap | Switch between the character view and the chat (transcript) view. This works in any state and keeps your place. If you followed the newest line, you stay on the newest line. The device drops the recording that the talk key press started. |
| Side key (tap) | Go to the next screen: chat, **Chats**, **Theme**, **Chirper**, **Text**, **Setup**, then the chat again. So **Setup** is five taps away. In **Chats**, the new chat is pinned on top, then come the recent chats that you can resume. Venice keeps only *+ New chat*. While the device thinks, a tap stops the turn. |
| Side key (hold) | Volume mode for 1.5 s. Turn the dial to change the volume (0-21, where 21 is unity gain). |
| Dial (turn) | Scroll the chat by 4, 8 or 16 lines for each click, depending on the spin speed. In character mode, the bubble column scrolls by 2, 4 or 8 lines. In the menu, the dial selects 1 row for each click, or 2 rows when you spin it fast. |

The **chirp! logo** is the boot splash. The boot messages are under it. The logo is also under the buttons of the Setup tab.

The empty chat has no bottom line. This is the chat before the first turn and after *+ New chat*. The device draws the logo as large as fits, centred, with the two hints (hold to talk, double-tap for voice mode). The logo moves up only when something starts: you talk, or voice mode waits for you. Errors still show on the bottom line.

## The menu tabs

### Theme

The **Theme** tab lists the six themes in alphabetical order: Catppuccin, Everforest, Gruvbox, Rosé Pine, Solarized and Tokyo Night. When you turn the dial, the screen switches to each theme as you pass it. The theme that you stop on stays. The device remembers it. The talk key switches the theme between dark and light. Light is easier to read outdoors.

### Chirper

The **Chirper** tab shows the character of the agent, large and animated. Turn the dial to browse. A counter-clockwise turn shows the next character. A quick spin jumps ten characters. The order is:

1. The kept look of the agent, but only if the agent has one.
2. *Original*.
3. The numbered characters `#1` to `#999`. The device makes them on the spot and stores none of them.

Press the talk key to keep the character on the screen for this agent. The device stores the character itself (its trait record), not a number. So the device draws a kept look in the same way after a firmware update. To clear the kept look, keep *Original* again.

If you leave the tab without a press, you keep the character that you had. Browsing is a preview.

The `#n` numbers only help you explore the current generator. They can show different characters after a firmware update.

*Original* belongs to the agent. If the agent has a persona look from `pair.py`, *Original* is that look. If not, *Original* is the character drawn from the profile name. The current generator always derives it.

The generator builds a character from these parts:

- 16 head shapes
- 8 torsos
- 5 kinds of feet
- 5 kinds of arms
- 19 ears, horns and antennae
- 14 eye styles
- 13 mouths
- 23 hats
- 19 accessories (worn or held)
- 14 coat patterns
- 30 colour palettes
- 7 temperaments

A temperament sets how the character blinks, glances and fidgets. A curious character looks about. A drowsy character yawns. A jumpy character shivers.

### Text

The **Text** tab lists the four fonts. Each row uses its own face:

- Atkinson Hyperlegible is the default. Its design aims at legibility.
- Inter.
- Paper Mono.
- Terminus is a pixel font that suits the character mode. It is drawn from its own hand-made bitmaps (8x14, 8x16 and 10x20), so every size is pixel-exact.

When you turn the dial, the whole UI switches to each font as you pass it. The talk key changes the size in the order S, M, L. The status line names the next size. The device remembers the font and the size.

The line height, the menu rows and the status strip follow the size. The setup screen keeps its own fixed font, so it always fits beside the QR code.

### Setup

The actions of the **Setup** tab are buttons:

- The agents: *NAME (in use)*, and *Switch to NAME* for the others. If there is one agent, the button reads *NAME (only agent)*.
- *View: Character* or *View: Chat*.
- In Venice mode, *Voice: NAME*. Each press changes the voice in the order Sky, Heart, Bella. The device says "Hi, I'm Chirp!" in the new voice and saves the choice.
- *Set up on phone*. The device restarts into the setup hotspot and shows the QR code.
- *Restart*. The device reboots to the boot screen. It keeps the settings, the chat and the kept look.

The chirp! logo is under the buttons. Below the logo is a dotted rule. Below the rule are plain rows with this information:

- the firmware version
- WiFi: the joined network and its signal, or the number of saved networks while the device is offline
- the address
- the Hermes host
- the speech server
- the battery

The dial scrolls through every row. At the larger text sizes, the info rows come into view too. The talk key does nothing on the info rows.

The *+ New chat* entry in the **Chats** tab is a button in the same way. A list that has more rows than fit shows a scrollbar at its right edge. This applies to Chats, Theme, Text and Setup. The Chats list loads in the background. So the side key moves on at once, also when Hermes is slow.

## Views

### View: Chat

**View: Chat** is the default view. It is the scrolling chat. The lines of the agent are on the left. Your lines are on the right. A rule separates them.

While you talk, the newest row is your level bars. They are on your side, under a rule, centred down to the bottom line. They appear after the same 0.5 s. While voice mode waits for you, the row shows three dots. While the agent speaks, its green wave is one blank line under the answer.

### View: Character

To switch to **View: Character**, use the talk key on the View row. Or hold the talk key and tap the side key anywhere. The switch applies at once, and the device remembers it.

This view replaces the scrolling chat with a pixel-art character for the agent in use. Every agent has its own character. The device draws it from the profile name, or from the look kept in the Chirper tab. The colours of the character are the same on every theme. The outline is the same on every theme.

Only the background follows the theme. The glow round the character follows the theme too. On dark themes, the glow is a ring of one pixel. It has the bright rim tint at half strength, just outside the deep outline. On light themes, there is no glow.

The view uses the whole screen:

- The character is on the left, centred.
- The chat is on the right, as comic speech bubbles. Each bubble is as tall as its text.
- The bubbles of the agent are on the left edge. Your bubbles are on the right and have a tail. They use the accent of the theme.
- The bubbles hold the whole stored chat, the same as the chat view. The oldest bubble is at the top.

The character idles: it breathes, blinks and glances. It reacts to these events:

- When you talk, the character perks up. Soft ripple rings light up one after another toward its head.
- While Hermes works, the character looks up. The chat shows its bubble with dots that light up in turn until the answer starts.
- While the answer plays, the character moves its mouth with the audio. Small sparks and notes rise beside it and fade.
- When a turn fails, the character shakes and has crossed eyes.
- When the screen dims, the character falls asleep.

Short one-shot expressions react to these events:

- You begin to talk: the character jumps and shows an exclamation mark.
- The answer starts: the character makes a happy bounce with sparkles.
- Nothing was heard: the character looks puzzled and shows a question mark.

While you talk, a bubble of yours at the bottom shows the level bars with the time under them ("Listening 4s"). In voice mode, "Closing 2s" counts down the silence that ends the turn. While voice mode waits for you, the bubble shows three dots that light up in turn. "Double-tap to exit" is under the dots. The bottom line of the chat view says the same.

While the agent speaks, a level meter in the same style runs along the bottom of the bubble of the answer.

The column follows the answer as it streams. While the device reads the answer out, the column scrolls with the speech. It keeps the spoken sentence near the top. The dial scrolls the whole column by 2, 4 or 8 lines for each click, depending on the spin speed. The next turn snaps the column back to the newest line.

This view has no bottom line. The bottom line comes up over the bottom only in these cases: an error, a volume change, or a running firmware update. It goes away with the next message.

## Status line and turns

The bottom line shows the status: Listening, Transcribing or Thinking. While the agent speaks, the wave row of the chat shows the status instead.

While a turn works, a bright band sweeps through the grey status word. The key combination for the view switch still works. A side key tap stops the turn ("Stopped", no answer).

Hermes can accept the question and then send nothing for 90 s. For example, its model is down. Then the turn ends as "Backend not answering".

The chat stays pinned to the newest turn. A scroll unpins it. The next question pins it again.

The talk key cannot interrupt an answer while the answer is still streaming. It can interrupt the answer after the streaming ends.

If a turn fails, the bottom line says why. These messages are possible:

- *Bad API key*, *Out of credit*, *Key not allowed* or *Rate limited* (Venice only).
- *Backend not answering*. Nothing arrived for 90 s.
- *Backend unreachable*.

See [Troubleshooting](troubleshooting.md).

## Boot check

At boot, and after the device wakes from sleep, the device checks its connections. The bottom line shows *Checking Venice* or *Checking Hermes*. If everything answers, the line shows *Connected to …* for a few seconds. If not, the line names the first problem. The conversations menu does the same. The check stops after about 15 s at the most.

The messages and their fixes are in [Troubleshooting](troubleshooting.md#boot-check-messages).

## Power and battery

### Power states

On battery, the device goes through these states:

- The screen dims after 60 s idle.
- The screen turns off at 3 minutes.
- The device light-sleeps at 10 minutes. A key wakes it, and the chat stays intact.
- The device deep-sleeps at 2 hours. A key reboots it.

On USB, the screen follows the same dim and off timeline, and any input wakes it. The device never sleeps.

The press that wakes a dark screen only wakes it.

The T-Deck has no charger chip. It counts as on USB while a USB host is attached. So a charger-only plug counts as battery.

### Battery level

In the chat view, the battery level is at the right end of the bottom line. It is also there when the bottom line comes up over the character view. The menus do not show it.

The battery level is a battery outline. Its inner bar shows the level at half strength. It uses the grey of the hints. There is no number.

The outline uses these colours:

- On USB, it turns the *Ok* colour. A bolt shows left of the outline while the charger works.
- On battery below 15 %, it turns the *Err* colour. It clears at 18 % or when you plug in USB.

The *Battery* row of the Setup tab shows the percentage, the cell voltage and whether the battery charges.

The percentage is an estimate from the cell voltage. The device reads the voltage from the charger every 10 s and smooths it. So the percentage moves a point at a time and ignores load dips. A loud answer or a fresh unplug can make it step.

In the character view on battery, the character slows to 4 frames a second after 20 s without input or a turn. This is well before the screen dims. The character returns to full speed on the next key, turn or word.
