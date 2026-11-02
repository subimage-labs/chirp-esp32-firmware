# Using chirp

Wait for the chat screen, then hold the talk key, ask something, release. The
answer is spoken as it arrives.

## Controls

| Action | T-Embed CC1101 Plus | T-Deck |
|---|---|---|
| Talk (hold, release to ask; press while it speaks to interrupt) | hold the encoder (dial) key | hold the **mic key** on the keyboard |
| Hands-free voice mode on/off | double-tap the dial key | double-tap the mic key |
| Menu: open, next tab, back to chat | tap the side key (5 taps reach **Setup**); a tap while it is thinking stops the turn | click the trackball (5 clicks reach **Setup**); **Backspace** leaves the menu |
| Select / scroll | turn the dial | roll the ball up/down (sideways does nothing) |
| Activate the selected row | press the dial key | **Enter** |
| Volume (0–21, 21 = unity gain) | hold the side key 1.5 s, turn the dial | hold the ball click, roll |
| Switch character view ↔ chat view | hold the dial key, tap the side key | hold the mic key, click the ball |
| Type a question | not available | type, **Enter** sends |

The rest of this page is written for the T-Embed's controls. On the T-Deck the
mic key is the dial key, the ball click is the side key and rolling the ball is
turning the dial.

### Details

| Input | Action |
|---|---|
| Dial key (hold) | Talk; release to ask. Press while it speaks to interrupt and ask again. The mic starts on the press, but the screen shows Listening only once you have held it 0.5 s, so taps never flash it; a press shorter than 0.3 s is never sent as speech |
| Dial key (double-tap) | Hands-free voice mode on/off (second press within 0.4 s): turns open when you speak and close after 2 s of silence; leaves after 3 min of quiet |
| Dial key held + side key tap | Switch between the character view and the chat (transcript) view, in any state, keeping your place (the newest if you were following it); what the dial press started recording is dropped |
| Side key (tap) | Steps through the screens: chat → **Chats** (new chat pinned on top, then recent chats to resume; Venice keeps only *+ New chat*) → **Theme** → **Chirper** → **Text** → **Setup** → chat, so Setup is five taps away. While it's thinking: stop the turn |
| Side key (hold) | Volume mode for 1.5 s; the dial adjusts (0–21, 21 = unity gain) |
| Dial (turn) | Scroll the chat: 4 / 8 / 16 lines per click by spin speed (the bubble column in character mode: 2 / 4 / 8). In the menu: select (1 row per click, 2 when spun) |

The **chirp! logo** is the boot splash, with the boot messages under it, and
sits under the Setup tab's buttons. The empty chat (before the first turn, and
after *+ New chat*) has no bottom line: the logo is drawn as large as fits,
centred with the two hints (hold to talk, double-tap for voice mode); it moves
up only when something starts (you talk, or voice mode waits for you). Errors
still come up on the bottom line.

## The menu tabs

### Theme

The **Theme** tab lists the six themes alphabetically (Catppuccin, Everforest,
Gruvbox, Rosé Pine, Solarized, Tokyo Night). Turning the dial switches the
screen to each as you pass it; the one you stop on stays (it is remembered).
The dial key flips the theme between dark and light; light reads better
outdoors.

### Chirper

The **Chirper** tab shows the agent's character large and animating. Turn the
dial to browse (counter-clockwise = next, a quick spin jumps ten): first the
agent's kept look (only while it has one), then *Original*, then the numbered
characters `#1` to `#999`, generated on the spot and none stored. Press the
dial key to keep the one on screen for this agent: the device stores the
character itself (its trait record), not a number, so a kept look is drawn
exactly the same after a firmware update. Keeping *Original* again clears the
kept look. Leaving the tab without pressing keeps the character you had:
browsing is a preview. The `#n` numbers are only a way to explore the current
generator and may show different characters after a firmware update.
*Original* is the agent's own: its persona look from `pair.py` when it has one,
else the character drawn from its profile name, always derived by the current
generator.

Characters are put together from 16 head shapes, 8 torsos, 5 kinds of feet and
5 of arms, 19 ears, horns and antennae, 14 eye styles, 13 mouths, 23 hats, 19
accessories (worn or held), 14 coat patterns, 30 colour palettes and 7
temperaments (how it blinks, glances and fidgets: a curious one looks about, a
drowsy one yawns, a jumpy one shivers).

### Text

The **Text** tab lists the four fonts, each row drawn in its own face:
Atkinson Hyperlegible (the default, designed for legibility), Inter, Paper Mono
and Terminus (a pixel font that suits the character mode: it is drawn from its
own hand-made 8x14, 8x16 and 10x20 bitmaps, so every size is pixel-exact).
Turning the dial switches the whole UI to each as you pass it; the dial key
cycles the size S → M → L (the status line names the next one). Both are
remembered. Line height, menu rows and the status strip follow the size; the
setup screen keeps its own fixed font so it always fits beside the QR code.

### Setup

The **Setup** tab's actions are drawn as buttons: the agents (*NAME (in use)*,
*Switch to NAME* for the others, or *NAME (only agent)* when there is one),
*View: Character* / *View: Chat*, in Venice mode *Voice: NAME* (each press
steps Sky → Heart → Bella and speaks "Hi, I'm Chirp!" in the new voice; the
choice is saved), *Set up on phone* (restarts into the setup hotspot and QR
code) and *Restart* (reboots to the boot screen; the settings, the chat and the
kept look stay). Under them sits the chirp! logo, then below the dotted rule,
as plain rows: the firmware version, WiFi (the joined network and its signal,
or how many are saved while offline), address, Hermes host, speech server and
battery. The dial scrolls through every row, so at the larger text sizes the
info rows come into view too (the dial key does nothing on them).

The **Chats** tab's *+ New chat* is a button the same way. Every list that
holds more rows than fit (Chats, Theme, Text, Setup) shows a scrollbar at its
right edge. The Chats list loads in the background, so the side key moves on at
once even when Hermes is slow.

## Views

**View: Chat** (the default) is the scrolling chat: the agent's lines on the
left, yours on the right, a rule between them. While you talk, its newest row
is your level bars (on your side, under a rule, centred down to the bottom
line, after the same 0.5 s); while voice mode waits for you, three dots there;
and while the agent speaks, its green wave a blank line under the answer.

**View: Character** (dial key on the View row, or the dial key held with a
side-key tap anywhere; applies at once and is remembered) replaces the
scrolling chat with a pixel-art character for the agent in use: every agent
gets its own, drawn from its profile name (or the look kept in the Chirper tab)
in its own colours, the same on every theme. Only the background, and with it
the glow round the character (on dark themes a one-pixel ring of its bright rim
tint at half strength just outside the deep outline; none on light themes),
follows the theme. The outline is the same on every theme.

The view takes the whole screen: the character on the left, centred, and the
chat on the right as comic speech bubbles, each as tall as its text: the
agent's on the left edge, yours on the right with a tail, tinted in the
theme's accent. The bubbles hold the whole stored chat (the same as the chat
view), oldest at the top.

The character idles (breathing, blinking, glancing), perks up while you talk
with soft ripple rings lighting one after another toward its head, looks up
while Hermes works (the chat shows its bubble with dots lighting in turn until
the answer starts), moves its mouth with the answer audio while little sparks
and notes rise beside it and fade, shakes with crossed eyes when a turn fails,
and falls asleep when the screen dims. Short one-shot expressions react to
events: a start (it jumps, with an exclamation mark) when you begin to talk, a
happy bounce with sparkles when the answer starts, and a puzzled look with a
question mark when nothing was heard.

While you talk, a bubble of yours at the bottom shows the level bars with the
time under them ("Listening 4s"; in voice mode "Closing 2s" counts down the
silence that ends the turn). While voice mode waits for you, that bubble shows
three dots lighting in turn, with "Double-tap to exit" under it (the chat
view's bottom line says it too). While it speaks, a level meter in the same
style runs along the bottom of the answer's bubble. The column follows the
answer as it streams, and while it is read out it scrolls along with the
speech, keeping the sentence being spoken near the top. The dial scrolls the
whole column (2 / 4 / 8 lines per click by spin speed) and the next turn snaps
back to the newest. There is no bottom line in this view: it comes up over the
bottom only for an error or while you change the volume or a firmware update
runs, and goes with the next message.

## Status line and turns

The bottom line is status (Listening / Transcribing / Thinking; while the agent
speaks the chat's wave row shows it instead). While a turn works, a bright band
sweeps through the grey status word; the view chord still switches the view,
and a side-key tap stops the turn ("Stopped", no answer). If Hermes accepts the
question but sends nothing for 90 s (its model is down, say), the turn ends as
"Backend not answering". The chat stays pinned to the newest turn; scrolling
unpins it and the next question re-pins it. While the answer is still
streaming, the dial key can't interrupt it; it can once streaming ends.

A turn that fails says why on the bottom line: *Bad API key*, *Out of credit*,
*Key not allowed* or *Rate limited* (Venice only), *Backend not answering*
(nothing arrived for 90 s) or *Backend unreachable*. See
[Troubleshooting](troubleshooting.md).

## Boot check

At boot (and after waking from sleep) the device checks its connections: the
bottom line says *Checking Venice* or *Checking Hermes*, then *Connected to …*
for a few seconds when everything answers, or names the first problem it finds
(the conversations menu does the same). The check gives up after about 15 s at
most. The messages and their fixes are in
[Troubleshooting](troubleshooting.md#boot-check-messages).

## Power and battery

**On battery** the screen dims after 60 s idle, turns off at 3 min, the device
light-sleeps at 10 min (a key wakes it with the chat intact) and deep-sleeps at
2 h (a key reboots it). On USB the screen follows the same dim and off timeline
(any input wakes it) but the device never sleeps. The press that wakes a dark
screen only wakes it. The T-Deck has no charger chip: it counts as on USB while
a USB host is attached, so a charger-only plug counts as battery.

**Battery level** sits at the right end of the bottom line in the chat view
(and when the bottom line comes up over the character view; the menus leave it
out): a battery outline whose inner bar, at half strength, shows the level, in
the grey of the hints. There is no number there. It turns the *Ok* colour on
USB, with a bolt left of the outline while the charger is working, and the
*Err* colour below 15 % on battery (it clears at 18 % or when USB is plugged
in). The Setup tab's *Battery* row gives the percentage, the cell voltage and
whether it is charging. The percentage is an estimate from the cell voltage
(read from the charger every 10 s and smoothed, so it moves a point at a time
and ignores load dips); a loud answer or a fresh unplug can make it step. With
the character screen on battery, the character slows to 4 frames a second after
20 s without input or a turn (well before the screen dims) and returns to full
speed on the next key, turn or word.
