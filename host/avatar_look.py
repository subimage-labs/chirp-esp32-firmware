"""Persona -> character look: the host half of the device's avatar (pure, no I/O).

The device draws a seeded character for every agent (firmware/include/avatar.h).
pair.py can refine it with a "look": a record of 12 trait indexes, one byte per
field, 0xFF = keep the seeded value. Only these indexes ever reach the device;
the persona text (SOUL.md) is read by pair.py on this machine, matched here
against the keyword table below, and never printed, stored or sent.

The rules are deliberately plain and transparent: RULES is an ordered table of
(label, regex, traits). A rule fires when its regex matches the profile name or
the persona text (case-insensitive); for every trait, the first rule that sets it
wins. Nothing else (no model, no scoring) decides what a character looks like,
so a surprising result can be explained by naming the rules that fired, and
corrected with `pair.py --look hat=none,palette=Teal`.

The vocabularies below must list exactly what avatar_art.h lists, in the same
order (host/test_avatar_look.py checks it against the header).
"""

import random
import re

FIELDS = ("head", "torso", "feet", "arms", "ears", "eyes", "mouth", "hat", "acc",
          "pattern", "palette", "temper")
KEEP = 0xFF
RECORD_LEN = len(FIELDS)

EARS = ["none", "cat", "bear", "bunny", "fox", "horns", "antenna", "fins", "droopy", "tufts",
        "elf", "discs", "bobbles", "spikes", "sprout", "mouse", "antlers", "buns", "unicorn"]
EYES = ["beady", "round", "tall", "wide", "shiny", "led", "dot", "sleepy", "cyclops", "slit",
        "heart", "joy", "gleam", "square"]
MOUTHS = ["smile", "flat", "cat", "beak", "grill", "tooth", "o", "fang", "tongueout", "zigzag",
          "pout", "grin", "smirk"]
HATS = ["none", "toque", "tophat", "wizard", "crown", "propeller", "headphones", "winged",
        "roof", "beanie", "bow", "halo", "hair", "cap", "cowboy", "party", "beret", "fez",
        "helmet", "mortarboard", "viking", "headband", "wreath"]
ACCS = ["none", "bowtie", "scarf", "glasses", "mustache", "scissors", "staff", "whisk", "mug",
        "flower", "monocle", "eyepatch", "necktie", "headset", "plaster", "book", "magnifier",
        "lollipop", "balloon"]
PATTERNS = ["none", "muzzle", "stripes", "spots", "mask", "blaze", "twotone", "blush",
            "freckles", "brows", "star", "frost", "belt", "split"]
TEMPERS = ["calm", "bouncy", "poised", "alert", "curious", "drowsy", "jumpy"]
PALETTES = ["Tangerine", "Pine", "Berry", "Sky", "Honey", "Plum", "Cherry", "Slate", "Jade",
            "Cocoa", "Indigo", "Salmon", "Teal", "Lilac", "Lime", "Denim", "Rose", "Sand",
            "Sage", "Coral", "Mustard", "Orchid", "Moss", "Graphite", "Peach", "Cobalt",
            "Copper", "Fern", "Wine", "Taupe"]
# Fields with a name vocabulary; head, torso, feet and arms are body shapes
# (counts only: they are never chosen from a persona, only by a reroll).
VOCAB = {"ears": EARS, "eyes": EYES, "mouth": MOUTHS, "hat": HATS, "acc": ACCS,
         "pattern": PATTERNS, "palette": PALETTES, "temper": TEMPERS}
COUNTS = {"head": 16, "torso": 8, "feet": 5, "arms": 5, "ears": len(EARS), "eyes": len(EYES),
          "mouth": len(MOUTHS), "hat": len(HATS), "acc": len(ACCS), "pattern": len(PATTERNS),
          "palette": len(PALETTES), "temper": len(TEMPERS)}

# Device composition rules (firmware traitsFromHash), mirrored by legible().
_TALL_EARS = ("bunny", "fox", "antlers")
_TALL_HATS = ("winged", "headphones", "tophat", "viking")
_CENTRED_EARS = ("antenna", "unicorn", "sprout", "spikes")

# (label, regex over "<profile name>\n<persona text>", traits it sets). Order is
# priority: the first rule to set a trait keeps it. Labels are what pair.py
# shows as the reason; they never include persona text.
RULES = [
    ("kitchen", r"\b(cook\w*|kitchen|culinary|recipes?|bak(?:er|ing)|restaurant|meals?)\b",
     dict(hat="toque", acc="whisk", ears="none", palette="Tangerine")),
    ("home", r"\b(home|house\w*|household|family|domestic)\b",
     dict(hat="roof", acc="mug", pattern="blush", temper="bouncy", palette="Honey")),
    ("cat", r"\b(cats?|kittens?|feline)\b",
     dict(ears="cat", mouth="cat", pattern="stripes")),
    ("dog", r"\b(dogs?|pupp(?:y|ies)|canine)\b",
     dict(ears="droopy", pattern="spots", palette="Cocoa")),
    ("salon", r"\b(salon|stylists?|hair\w*|beauty|barber)\b",
     dict(acc="scissors", hat="bow", temper="poised", palette="Rose")),
    ("garden", r"\b(garden\w*|plants?|flowers?|botan\w*|farm\w*)\b",
     dict(acc="flower", palette="Pine", ears="none")),
    ("code", r"\b(code|coding|programm\w*|developers?|software|engineer\w*|debug\w*|devops)\b",
     dict(hat="headphones", acc="glasses", eyes="round", palette="Slate", temper="calm")),
    ("music", r"\b(music\w*|songs?|playlists?|dj|podcasts?)\b",
     dict(hat="headphones", palette="Plum")),
    ("magic", r"\b(wizard|magic\w*|spells?|sorcer\w*|fantasy|stories|storytell\w*)\b",
     dict(hat="wizard", acc="staff", palette="Indigo")),
    ("study", r"\b(teach\w*|tutor\w*|school|students?|homework|learn\w*|lessons?)\b",
     dict(acc="glasses", hat="beanie", palette="Denim", eyes="round")),
    ("science", r"\b(scien\w*|research\w*|laborator\w*|physics|chemistry)\b",
     dict(acc="glasses", eyes="shiny", palette="Teal")),
    ("money", r"\b(financ\w*|money|budget\w*|account(?:ant|ing)|invest\w*|bank\w*)\b",
     dict(hat="tophat", acc="bowtie", palette="Sand", temper="poised")),
    ("health", r"\b(health\w*|medic\w*|doctor|nurse|fitness|wellness|sleep)\b",
     dict(hat="halo", pattern="blush", palette="Jade")),
    ("weather", r"\b(weather|forecast|outdoors?|travel\w*)\b",
     dict(acc="scarf", palette="Sky")),
    ("security", r"\b(secur\w*|guard\w*|watch(?:dog|ful)|vigilan\w*|alarm)\b",
     dict(temper="alert", eyes="tall", hat="beanie", palette="Slate")),
    ("playful", r"\b(playful|fun|funny|jokes?|cheerful|bubbly|upbeat|friendly|warm)\b",
     dict(temper="bouncy", mouth="smile", pattern="blush")),
    ("formal", r"\b(formal|professional|polished|upscale|refined|elegant|courteous)\b",
     dict(temper="poised", acc="bowtie")),
    ("terse", r"\b(terse|concise|brief|direct|no filler)\b",
     dict(temper="calm", mouth="flat")),
    ("cowboy", r"\b(cowboys?|cowgirls?|western|ranch\w*|rodeo)\b",
     dict(hat="cowboy", palette="Copper")),
    ("party", r"\b(party|parties|birthdays?|celebrat\w*)\b",
     dict(hat="party", acc="balloon", temper="bouncy")),
    ("books", r"\b(books?|librar\w*|novels?|literature|archiv\w*|paperwork)\b",
     dict(acc="book", palette="Taupe")),
    ("detective", r"\b(detectives?|investigat\w*|mystery|mysteries|clues?|sleuth\w*)\b",
     dict(acc="magnifier", eyes="gleam")),
    ("butler", r"\b(butler|concierge|valet|lawyers?|legal|attorney)\b",
     dict(acc="monocle", temper="poised")),
    ("pirate", r"\b(pirates?|sailors?|sail\w*|nautical)\b",
     dict(acc="eyepatch", hat="headband", palette="Cobalt")),
    ("robot", r"\b(robots?|androids?|automation|machines?|mechanical|mecha)\b",
     dict(ears="discs", eyes="square", mouth="grill", palette="Graphite")),
    ("fox", r"\b(fox(?:es)?|wolf|wolves)\b",
     dict(ears="fox", palette="Coral")),
    ("bear", r"\b(bears?|pandas?|teddy)\b",
     dict(ears="bear", palette="Cocoa")),
    ("deer", r"\b(deer|elk|moose|reindeer|antler\w*)\b",
     dict(ears="antlers")),
    ("unicorn", r"\b(unicorns?|rainbows?)\b",
     dict(ears="unicorn", palette="Orchid")),
    ("elf", r"\b(elf|elves|elven|fairy|fairies|fae|pixie\w*)\b",
     dict(ears="elf", hat="wreath", palette="Sage")),
    ("ocean", r"\b(ocean\w*|sea|fish|swim\w*|diving|scuba|aquatic)\b",
     dict(ears="fins", palette="Teal")),
    ("viking", r"\b(vikings?|norse|nordic)\b",
     dict(hat="viking", palette="Wine")),
    ("sport", r"\b(sport\w*|coach\w*|gym|workouts?|running|athlet\w*)\b",
     dict(hat="headband", pattern="brows", temper="jumpy")),
    ("workshop", r"\b(builders?|construction|repair\w*|workshop|diy|handyman|carpent\w*|plumb\w*)\b",
     dict(hat="helmet", pattern="belt", palette="Mustard")),
    ("radio", r"\b(radio|broadcast\w*|announcers?|interview\w*|streamers?|dispatch\w*)\b",
     dict(acc="headset")),
    ("romance", r"\b(romantic|romance|valentines?|dating|sweetheart)\b",
     dict(eyes="heart", pattern="blush", palette="Rose")),
    ("treats", r"\b(hungry|snack\w*|desserts?|candy|candies|sweets|lollipops?)\b",
     dict(mouth="tongueout", acc="lollipop", palette="Peach")),
    ("sly", r"\b(sarcas\w*|snark\w*|wry|sly|mischiev\w*|trickster)\b",
     dict(mouth="smirk", pattern="brows")),
    ("zen", r"\b(calm|zen|mindful\w*|meditat\w*|serene|gentle)\b",
     dict(eyes="joy")),
    ("curious", r"\b(curious|curiosity|explor\w*|wonder\w*|discover\w*)\b",
     dict(temper="curious", eyes="gleam")),
    ("bedtime", r"\b(bedtime|lullab\w*|dream\w*|nighttime|drowsy|cozy|sleepy)\b",
     dict(temper="drowsy", eyes="sleepy")),
    ("kids", r"\b(kids?|child\w*|toddlers?|babies|baby)\b",
     dict(mouth="pout", pattern="freckles", temper="bouncy")),
    ("graduate", r"\b(graduat\w*|academic\w*|scholar\w*|university|professor\w*)\b",
     dict(hat="mortarboard")),
    ("artist", r"\b(french|paris|artists?|painters?|painting|sculpt\w*|creative)\b",
     dict(hat="beret")),
    ("streetwear", r"\b(baseball|streetwear|skater|skateboard\w*|sneakers?)\b",
     dict(hat="cap", pattern="freckles")),
    ("bazaar", r"\b(turkish|egypt\w*|moroccan|bazaar|marrakech)\b",
     dict(hat="fez")),
    ("first-aid", r"\b(first aid|bandages?|clumsy|accident-prone)\b",
     dict(acc="plaster")),
    ("stars", r"\b(stars?|celebrit\w*|famous|fame|astro\w*|cosmic|galax\w*|spaceships?|outer space)\b",
     dict(pattern="star", palette="Indigo")),
    ("winter", r"\b(winter|snow\w*|cold|ice|frost\w*|arctic|polar)\b",
     dict(pattern="frost", palette="Sky")),
    ("twins", r"\b(twins?|dual|duo|two-faced)\b",
     dict(pattern="split")),
    ("spooky", r"\b(vampir\w*|dracula|spooky|halloween|monsters?|zombies?|ghouls?|bats?)\b",
     dict(mouth="fang")),
    ("glitch", r"\b(glitch\w*|chaos|chaotic|nervous|anxious|jittery)\b",
     dict(mouth="zigzag", temper="jumpy")),
    ("cheeky", r"\b(cheeky|sassy|grin\w*|daring|swagger|cocky)\b",
     dict(mouth="grin")),
    ("camera", r"\b(cyclops|cameras?|photograph\w*|lens|lenses)\b",
     dict(eyes="cyclops")),
    ("reptile", r"\b(dragons?|lizards?|reptil\w*|snakes?|serpents?|gecko\w*)\b",
     dict(eyes="slit", ears="spikes")),
    ("necktie", r"\b(corporate|executives?|managers?|sales|office|meetings?|business)\b",
     dict(acc="necktie")),
    ("forest", r"\b(forest|woods|woodland|camping|campers?|campfire|moss)\b",
     dict(palette="Moss")),
    ("mouse", r"\b(mice|hamsters?|gerbils?|rodents?|rats?|cheese)\b",
     dict(ears="mouse")),
    ("sprout", r"\b(sprouts?|sprouting|seedlings?|seeds?|spring|nursery)\b",
     dict(ears="sprout", palette="Fern")),
    ("buns", r"\b(anime|manga|kawaii|ballet|ballerinas?|dancers?|dumplings?|buns?)\b",
     dict(ears="buns")),
    ("bobbles", r"\b(aliens?|insects?|snails?|jester|bobbles?|pom-?poms?)\b",
     dict(ears="bobbles")),
    # The Hermes boilerplate persona and nothing else fired: the messenger god.
    ("messenger", r"\bhermes\b", dict(hat="winged", acc="staff", ears="none")),
]
_COMPILED = [(label, re.compile(rx, re.I), traits) for label, rx, traits in RULES]


def _index(field, name):
    vocab = VOCAB[field]
    for i, v in enumerate(vocab):
        if v.lower() == name.lower():
            return i
    raise ValueError(f"{field}: no '{name}' (one of {', '.join(vocab)})")


def empty_record():
    return [KEEP] * RECORD_LEN


def derive(profile, soul_text):
    """-> (record, labels): the look the rules give this profile and persona.

    `profile` is the profile name ("" for the default), `soul_text` the persona
    (may be empty). `labels` names the rules that fired, in priority order.
    """
    haystack = f"{profile}\n{soul_text or ''}"
    rec = empty_record()
    fired = []
    for label, rx, traits in _COMPILED:
        if label == "messenger" and fired:
            continue
        if not rx.search(haystack):
            continue
        used = False
        for field, name in traits.items():
            slot = FIELDS.index(field)
            if rec[slot] == KEEP:
                rec[slot] = _index(field, name)
                used = True
        if used:
            fired.append(label)
    return legible(rec), fired


def legible(rec):
    """The device's composition rules, for records that name the parts
    directly (a KEEP part is left to the device, which applies them to the
    seeded value): tall ears do not fit under tall hats, centred ears
    (antenna, unicorn horn, sprout, spikes) clash with any hat but a bow or
    halo, and an eyepatch would hide a cyclops' only eye. Returns a new record."""
    r = list(rec)
    slot = FIELDS.index("ears")
    ears, hat = r[slot], r[FIELDS.index("hat")]
    if ears != KEEP and hat != KEEP:
        if HATS[hat] in _TALL_HATS and EARS[ears] in _TALL_EARS:
            r[slot] = 0
        if EARS[ears] in _CENTRED_EARS and HATS[hat] not in ("none", "bow", "halo"):
            r[slot] = 0
    eyes, acc = r[FIELDS.index("eyes")], r[FIELDS.index("acc")]
    if eyes != KEEP and acc != KEEP and EYES[eyes] == "cyclops" and ACCS[acc] == "eyepatch":
        r[FIELDS.index("acc")] = 0
    return r


def apply_edits(rec, spec):
    """`hat=toque,palette=Teal` -> record with those fields set; `field=keep`
    unsets one. Raises ValueError with the allowed names on a bad spec."""
    r = list(rec)
    for part in filter(None, (p.strip() for p in spec.split(","))):
        field, sep, name = part.partition("=")
        field, name = field.strip().lower(), name.strip()
        if not sep or field not in VOCAB:
            raise ValueError(f"'{part}': use field=name with field one of {', '.join(VOCAB)}")
        r[FIELDS.index(field)] = KEEP if name.lower() == "keep" else _index(field, name)
    return legible(r)


def reroll(rec, profile, n):
    """A different look with the persona's own traits kept: every field the
    rules left open is drawn at random, seeded by (profile, n) so the same
    number gives the same look. Returns a new record."""
    rng = random.Random(f"{profile}\0{n}")
    r = list(rec)
    for slot, field in enumerate(FIELDS):
        draw = rng.randrange(COUNTS[field])        # always draw: later fields stay stable
        if r[slot] == KEEP:
            r[slot] = draw
    return legible(r)


def to_hex(rec):
    return "".join(f"{b:02x}" for b in rec)


def from_hex(text):
    if len(text) != RECORD_LEN * 2:
        raise ValueError("a look is %d hex digits" % (RECORD_LEN * 2))
    return [int(text[i:i + 2], 16) for i in range(0, len(text), 2)]


_HAT_WORDS = {"toque": "a toque", "tophat": "a top hat", "wizard": "a wizard hat",
              "crown": "a crown", "propeller": "a propeller cap", "headphones": "headphones",
              "winged": "a winged cap", "roof": "a little roof hat", "beanie": "a beanie",
              "bow": "a bow", "halo": "a halo", "hair": "tidy hair", "cap": "a baseball cap",
              "cowboy": "a cowboy hat", "party": "a party hat", "beret": "a beret",
              "fez": "a fez", "helmet": "a helmet", "mortarboard": "a mortarboard",
              "viking": "a viking helmet", "headband": "a headband", "wreath": "a wreath"}
_ACC_WORDS = {"bowtie": "a bow tie", "scarf": "a scarf", "glasses": "glasses",
              "mustache": "a moustache", "scissors": "scissors", "staff": "a staff",
              "whisk": "a whisk", "mug": "a mug", "flower": "a flower", "monocle": "a monocle",
              "eyepatch": "an eyepatch", "necktie": "a necktie", "headset": "a headset",
              "plaster": "a plaster", "book": "a book", "magnifier": "a magnifying glass",
              "lollipop": "a lollipop", "balloon": "a balloon"}
_HELD_ACCS = ("scissors", "staff", "whisk", "mug", "flower", "book", "magnifier", "lollipop",
              "balloon")
_EAR_WORDS = {"none": "no ears", "cat": "cat ears", "bear": "bear ears", "bunny": "bunny ears",
              "fox": "fox ears", "horns": "little horns", "antenna": "an antenna", "fins": "fins",
              "droopy": "droopy ears", "tufts": "ear tufts", "elf": "elf ears",
              "discs": "disc ears", "bobbles": "bobble ears", "spikes": "spiky ears",
              "sprout": "a sprout", "mouse": "mouse ears", "antlers": "antlers",
              "buns": "hair buns", "unicorn": "a unicorn horn"}
_EYE_WORDS = {"beady": "beady eyes", "round": "round eyes", "tall": "tall eyes",
              "wide": "wide eyes", "shiny": "shiny eyes", "led": "LED eyes", "dot": "dot eyes",
              "sleepy": "sleepy eyes", "cyclops": "a cyclops eye", "slit": "slit eyes",
              "heart": "heart eyes", "joy": "joyful eyes", "gleam": "gleaming eyes",
              "square": "square eyes"}
_MOUTH_WORDS = {"smile": "a smile", "flat": "a straight mouth", "cat": "a cat mouth",
                "beak": "a beak", "grill": "a grille mouth", "tooth": "a toothy grin",
                "o": "a round mouth", "fang": "little fangs", "tongueout": "its tongue out",
                "zigzag": "a zigzag mouth", "pout": "a pout", "grin": "a wide grin",
                "smirk": "a smirk"}
_PATTERN_WORDS = {"muzzle": "a pale muzzle", "stripes": "stripes", "spots": "spots",
                  "mask": "a mask", "blaze": "a blaze", "twotone": "two-tone colouring",
                  "blush": "rosy cheeks", "freckles": "freckles", "brows": "bold brows",
                  "star": "a star mark", "frost": "frosty tips", "belt": "a belt",
                  "split": "split colouring"}


def describe(rec):
    """A short sentence for the owner, e.g. "a Teal character with cat ears,
    wearing a toque and holding a whisk, poised". Names traits only."""
    def get(field):
        v = rec[FIELDS.index(field)]
        return None if v == KEEP or v >= COUNTS[field] else v

    pal = get("palette")
    head = f"a {PALETTES[pal]}" if pal is not None else "a"
    bits = []
    for field, words, vocab in (("ears", _EAR_WORDS, EARS), ("eyes", _EYE_WORDS, EYES),
                                ("mouth", _MOUTH_WORDS, MOUTHS),
                                ("pattern", _PATTERN_WORDS, PATTERNS)):
        v = get(field)
        if v is not None and vocab[v] in words:
            bits.append(words[vocab[v]])
    worn = []
    v = get("hat")
    if v is not None and HATS[v] in _HAT_WORDS:
        worn.append("wearing " + _HAT_WORDS[HATS[v]])
    v = get("acc")
    if v is not None and ACCS[v] in _ACC_WORDS:
        verb = "holding" if ACCS[v] in _HELD_ACCS else "with"
        worn.append(f"{verb} {_ACC_WORDS[ACCS[v]]}")
    text = f"{head} character"
    if bits:
        text += " with " + ", ".join(bits)
    if worn:
        text += ", " + " and ".join(worn)
    v = get("temper")
    if v is not None:
        text += f", {TEMPERS[v]}"
    if all(b == KEEP for b in rec):
        return "its seeded default (no persona hints found)"
    return text
