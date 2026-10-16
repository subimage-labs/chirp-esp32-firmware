#!/usr/bin/env python3
"""Tests for host/avatar_look.py (run by firmware/test/host/run.sh).

Personas here are synthetic; they are not anyone's SOUL.md.
"""

import os
import re
import sys
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import avatar_look as al  # noqa: E402

ART = os.path.join(HERE, "..", "firmware", "include", "avatar_art.h")


def enum_names(src, enum):
    body = re.search(r"enum %s : uint8_t \{(.*?)\};" % enum, src, re.S).group(1)
    names = [n.strip() for n in re.sub(r"//[^\n]*", "", body).replace("\n", " ").split(",")]
    return [n for n in names if n and not n.endswith("Count")]


class Vocabulary(unittest.TestCase):
    """The host's name lists are the device's, in the device's order."""

    def setUp(self):
        with open(ART) as f:
            self.src = f.read()

    def check(self, enum, vocab, aliases=None):
        names = enum_names(self.src, enum)
        self.assertEqual(len(names), len(vocab), enum)
        for dev, host in zip(names, vocab):
            d = dev.lower()
            d = (aliases or {}).get(d, d)
            self.assertEqual(d.replace("_", ""), host.replace("_", ""), enum)

    def test_enums(self):
        self.check("Ears", al.EARS, {"earsnone": "none"})
        self.check("Eyes", al.EYES)
        self.check("Mouths", al.MOUTHS, {"catmouth": "cat", "omouth": "o"})
        self.check("Hats", al.HATS, {"hatnone": "none"})
        self.check("Accs", al.ACCS, {"accnone": "none"})
        self.check("Patterns", al.PATTERNS, {"patnone": "none"})
        self.check("Tempers", al.TEMPERS)

    def test_palettes_and_counts(self):
        names = re.findall(r'^\s*\{"([A-Za-z ]+)", 0x', self.src, re.M)
        self.assertEqual(names, al.PALETTES)
        self.assertEqual(int(re.search(r"PALETTE_COUNT = (\d+)", self.src).group(1)),
                         len(al.PALETTES))
        for const, field in (("HEAD", "head"), ("TORSO", "torso"), ("FEET", "feet"),
                             ("ARMS", "arms")):
            self.assertEqual(int(re.search(r"%s_COUNT = (\d+)" % const, self.src).group(1)),
                             al.COUNTS[field], field)

    def test_fields_match_the_device_record(self):
        with open(os.path.join(HERE, "..", "firmware", "include", "avatar.h")) as f:
            avatar_h = f.read()
        self.assertEqual(int(re.search(r"TRAIT_FIELDS = (\d+)", avatar_h).group(1)), al.RECORD_LEN)
        self.assertEqual(al.FIELDS.index("palette"),
                         int(re.search(r"TRAIT_PALETTE = (\d+)", avatar_h).group(1)))
        order = re.search(r"&head, &torso.*?&temper\};", avatar_h, re.S).group(0)
        self.assertEqual(re.findall(r"&(\w+)", order), list(al.FIELDS))


class Rules(unittest.TestCase):
    def rec(self, profile, text=""):
        return al.derive(profile, text)

    def field(self, rec, name, vocab):
        v = rec[al.FIELDS.index(name)]
        return None if v == al.KEEP else vocab[v]

    def test_a_kitchen_persona(self):
        rec, fired = self.rec("alpha", "You help in the kitchen: recipes, timers, shopping lists.")
        self.assertEqual(fired[0], "kitchen")
        self.assertEqual(self.field(rec, "hat", al.HATS), "toque")
        self.assertEqual(self.field(rec, "acc", al.ACCS), "whisk")
        self.assertEqual(self.field(rec, "palette", al.PALETTES), "Tangerine")
        self.assertEqual(rec[al.FIELDS.index("head")], al.KEEP)       # body shape stays seeded

    def test_the_profile_name_counts(self):
        rec, fired = self.rec("garden-helper", "Be brief.")
        self.assertIn("garden", fired)
        self.assertEqual(self.field(rec, "acc", al.ACCS), "flower")

    def test_first_rule_wins_per_trait(self):
        # kitchen and home both set the hat; kitchen is earlier.
        rec, fired = self.rec("x", "A friendly home kitchen assistant.")
        self.assertEqual(self.field(rec, "hat", al.HATS), "toque")
        self.assertEqual(self.field(rec, "pattern", al.PATTERNS), "blush")   # only home sets it
        self.assertEqual(fired[:2], ["kitchen", "home"])

    def test_no_hints_keeps_the_seeded_character(self):
        rec, fired = self.rec("nova", "Answer questions.")
        self.assertEqual(rec, [al.KEEP] * al.RECORD_LEN)
        self.assertEqual(fired, [])
        self.assertIn("seeded default", al.describe(rec))

    def test_the_boilerplate_persona_is_the_messenger(self):
        rec, fired = self.rec("", "You are Hermes Agent, an assistant.")
        self.assertEqual(fired, ["messenger"])
        self.assertEqual(self.field(rec, "hat", al.HATS), "winged")
        # ...but only when nothing more specific fired.
        rec, fired = self.rec("", "You are Hermes Agent. You help with cooking.")
        self.assertEqual(fired, ["kitchen"])

    def test_whole_words_only(self):
        _, fired = self.rec("x", "Concatenate strings; dogma; scatter plots.")
        self.assertEqual(fired, [])

    def test_legibility_rules(self):
        rec = al.empty_record()
        rec[al.FIELDS.index("ears")] = al.EARS.index("bunny")
        rec[al.FIELDS.index("hat")] = al.HATS.index("winged")
        self.assertEqual(al.legible(rec)[al.FIELDS.index("ears")], 0)
        rec[al.FIELDS.index("hat")] = al.HATS.index("crown")
        self.assertEqual(al.legible(rec)[al.FIELDS.index("ears")], al.EARS.index("bunny"))

    def legible_ears(self, ears, hat):
        rec = al.empty_record()
        if ears is not None:
            rec[al.FIELDS.index("ears")] = al._index("ears", ears)
        if hat is not None:
            rec[al.FIELDS.index("hat")] = al._index("hat", hat)
        out = al.legible(rec)
        self.assertEqual(out[al.FIELDS.index("hat")], rec[al.FIELDS.index("hat")])
        return self.field(out, "ears", al.EARS)

    def test_tall_ears_do_not_fit_under_tall_hats(self):
        for ears in ("bunny", "fox", "antlers"):
            for hat in ("winged", "headphones", "tophat", "viking"):
                self.assertEqual(self.legible_ears(ears, hat), "none", (ears, hat))
            for hat in ("crown", "bow", "halo", "beanie", "none"):
                self.assertEqual(self.legible_ears(ears, hat), ears, (ears, hat))
        self.assertEqual(self.legible_ears("cat", "viking"), "cat")   # not a tall ear

    def test_centred_ears_clash_with_any_hat_but_bow_halo_or_none(self):
        for ears in ("antenna", "unicorn", "sprout", "spikes"):
            for hat in al.HATS:
                want = ears if hat in ("none", "bow", "halo") else "none"
                self.assertEqual(self.legible_ears(ears, hat), want, (ears, hat))
        self.assertEqual(self.legible_ears("antenna", "propeller"), "none")
        self.assertEqual(self.legible_ears("elf", "wreath"), "elf")   # not a centred ear

    def test_legibility_leaves_keep_to_the_device(self):
        self.assertEqual(self.legible_ears(None, "viking"), None)
        self.assertEqual(self.legible_ears("unicorn", None), "unicorn")
        self.assertEqual(self.legible_ears("antlers", None), "antlers")
        self.assertEqual(al.legible(al.empty_record()), al.empty_record())

    def test_an_eyepatch_does_not_hide_a_cyclops_eye(self):
        rec = al.empty_record()
        rec[al.FIELDS.index("eyes")] = al.EYES.index("cyclops")
        rec[al.FIELDS.index("acc")] = al.ACCS.index("eyepatch")
        self.assertEqual(al.legible(rec)[al.FIELDS.index("acc")], 0)
        rec[al.FIELDS.index("eyes")] = al.EYES.index("round")
        self.assertEqual(al.legible(rec)[al.FIELDS.index("acc")], al.ACCS.index("eyepatch"))
        rec[al.FIELDS.index("eyes")] = al.KEEP
        self.assertEqual(al.legible(rec)[al.FIELDS.index("acc")], al.ACCS.index("eyepatch"))

    def test_derive_applies_legibility_to_rule_traits(self):
        # reptile gives spikes ears, pirate a headband: centred ears lose to the hat.
        rec, fired = self.rec("x", "A pirate with a pet dragon.")
        self.assertEqual(self.field(rec, "hat", al.HATS), "headband")
        self.assertEqual(self.field(rec, "ears", al.EARS), "none")
        self.assertEqual(self.field(rec, "eyes", al.EYES), "slit")
        # tall fox ears under a viking helmet.
        rec, _ = self.rec("x", "A viking who loves a fox.")
        self.assertEqual(self.field(rec, "hat", al.HATS), "viking")
        self.assertEqual(self.field(rec, "ears", al.EARS), "none")

    def test_every_rule_names_real_traits(self):
        for label, rx, traits in al.RULES:
            for field, name in traits.items():
                al._index(field, name)                                # raises when unknown

    # label -> (a synthetic sentence that fires only that rule, the traits it must give)
    NEW_RULES = {
        "cowboy": ("A rodeo companion.", dict(hat="cowboy", palette="Copper")),
        "party": ("Plans every birthday.", dict(hat="party", acc="balloon", temper="bouncy")),
        "books": ("Knows every novel.", dict(acc="book", palette="Taupe")),
        "detective": ("Follows each clue.", dict(acc="magnifier", eyes="gleam")),
        "butler": ("A polite valet.", dict(acc="monocle", temper="poised")),
        "pirate": ("A salty sailor.", dict(acc="eyepatch", hat="headband", palette="Cobalt")),
        "robot": ("A tidy android.", dict(ears="discs", eyes="square", mouth="grill",
                                          palette="Graphite")),
        "fox": ("Quick like a fox.", dict(ears="fox", palette="Coral")),
        "bear": ("Big as a panda.", dict(ears="bear", palette="Cocoa")),
        "deer": ("A shy moose.", dict(ears="antlers")),
        "unicorn": ("Loves a rainbow.", dict(ears="unicorn", palette="Orchid")),
        "elf": ("A forest pixie.", dict(ears="elf", hat="wreath", palette="Sage")),
        "ocean": ("Knows the sea.", dict(ears="fins", palette="Teal")),
        "viking": ("A norse saga voice.", dict(hat="viking", palette="Wine")),
        "sport": ("Your gym buddy.", dict(hat="headband", pattern="brows", temper="jumpy")),
        "workshop": ("Handyman advice.", dict(hat="helmet", pattern="belt", palette="Mustard")),
        "radio": ("Sounds like a radio voice.", dict(acc="headset")),
        "romance": ("A romantic poet.", dict(eyes="heart", pattern="blush", palette="Rose")),
        "treats": ("Always hungry.", dict(mouth="tongueout", acc="lollipop", palette="Peach")),
        "sly": ("Dry and sly.", dict(mouth="smirk", pattern="brows")),
        "zen": ("Speaks with zen.", dict(eyes="joy")),
        "curious": ("Endlessly curious.", dict(temper="curious", eyes="gleam")),
        "bedtime": ("Reads a lullaby.", dict(temper="drowsy", eyes="sleepy")),
        "kids": ("For toddlers.", dict(mouth="pout", pattern="freckles", temper="bouncy")),
        "graduate": ("Helps a scholar.", dict(hat="mortarboard")),
        "artist": ("A painter's aide.", dict(hat="beret")),
        "streetwear": ("Wears sneakers.", dict(hat="cap", pattern="freckles")),
        "bazaar": ("Haggles in a bazaar.", dict(hat="fez")),
        "first-aid": ("Carries a bandage.", dict(acc="plaster")),
        "stars": ("Sings of the cosmic.", dict(pattern="star", palette="Indigo")),
        "winter": ("A snowy voice.", dict(pattern="frost", palette="Sky")),
        "twins": ("One of the twins.", dict(pattern="split")),
        "spooky": ("A pale vampire.", dict(mouth="fang")),
        "glitch": ("A bit jittery.", dict(mouth="zigzag", temper="jumpy")),
        "cheeky": ("Quite cheeky.", dict(mouth="grin")),
        "camera": ("Behind the lens.", dict(eyes="cyclops")),
        "reptile": ("A gecko fan.", dict(eyes="slit", ears="spikes")),
        "necktie": ("A sales assistant.", dict(acc="necktie")),
        "forest": ("Goes camping.", dict(palette="Moss")),
        "mouse": ("A hamster keeper.", dict(ears="mouse")),
        "sprout": ("Tends a seedling.", dict(ears="sprout", palette="Fern")),
        "buns": ("Loves dumplings.", dict(ears="buns")),
        "bobbles": ("Waves to an alien.", dict(ears="bobbles")),
    }

    def test_each_new_rule_fires_and_sets_its_traits(self):
        for label, (text, want) in self.NEW_RULES.items():
            rec, fired = self.rec("x", text)
            self.assertEqual(fired, [label], text)
            expected = al.empty_record()
            for field, name in want.items():
                expected[al.FIELDS.index(field)] = al._index(field, name)
            self.assertEqual(rec, al.legible(expected), label)

    def test_the_new_rules_are_covered_by_the_table_and_come_before_the_messenger(self):
        labels = [label for label, _, _ in al.RULES]
        self.assertEqual(labels[-1], "messenger")
        self.assertEqual(set(labels[labels.index("terse") + 1:-1]), set(self.NEW_RULES))
        self.assertEqual(len(labels), len(set(labels)))
        self.assertEqual(labels[:labels.index("terse") + 1][:2], ["kitchen", "home"])

    def test_every_new_part_is_reachable_from_some_rule(self):
        # indexes at or past these are the parts the device vocabularies gained
        old = {"ears": 10, "eyes": 7, "mouth": 7, "hat": 13, "acc": 10, "pattern": 8,
               "palette": 18, "temper": 4}
        used = {field: set() for field in old}
        for _, _, traits in al.RULES:
            for field, name in traits.items():
                used[field].add(al._index(field, name))
        for field, first_new in old.items():
            missing = [al.VOCAB[field][i] for i in range(first_new, al.COUNTS[field])
                       if i not in used[field]]
            self.assertEqual(missing, [], field)

    def test_new_rules_match_whole_words_only(self):
        _, fired = self.rec("x", "Starch, stardust, slice, dice, colder, foxtrot, radiology, "
                                 "radiant, bookkeeping, assailant, ratio, batch, rodeos, "
                                 "dicey, seedy, gymnastics, duodenum, framed.")
        self.assertEqual(fired, [])
        # a hyphen is a word boundary, so a compound still contains the whole word
        _, fired = self.rec("x", "A cold-blooded narrator.")
        self.assertEqual(fired, ["winter"])

    def test_rules_never_override_an_earlier_rule(self):
        # kitchen (earlier) owns hat and palette; party only adds what is still open.
        rec, fired = self.rec("x", "A kitchen helper for every birthday.")
        self.assertEqual(self.field(rec, "hat", al.HATS), "toque")
        self.assertEqual(self.field(rec, "acc", al.ACCS), "whisk")
        self.assertEqual(self.field(rec, "temper", al.TEMPERS), "bouncy")
        self.assertEqual(fired, ["kitchen", "party"])

    def test_a_new_rule_suppresses_the_messenger(self):
        _, fired = self.rec("", "You are Hermes Agent. A rodeo companion.")
        self.assertEqual(fired, ["cowboy"])


class Records(unittest.TestCase):
    def test_hex_round_trip_and_size(self):
        rec, _ = al.derive("alpha", "kitchen")
        text = al.to_hex(rec)
        self.assertEqual(len(text), 24)
        self.assertEqual(al.from_hex(text), rec)
        self.assertTrue(all(0 <= b <= 255 for b in rec))
        for field in al.VOCAB:
            v = rec[al.FIELDS.index(field)]
            self.assertTrue(v == al.KEEP or v < al.COUNTS[field])

    def test_edits(self):
        rec, _ = al.derive("alpha", "kitchen")
        out = al.apply_edits(rec, "hat=beanie, palette=teal ,acc=keep")
        self.assertEqual(out[al.FIELDS.index("hat")], al.HATS.index("beanie"))
        self.assertEqual(out[al.FIELDS.index("palette")], al.PALETTES.index("Teal"))
        self.assertEqual(out[al.FIELDS.index("acc")], al.KEEP)
        for bad in ("hat=sombrero", "wings=2", "hat", "palette=Chartreuse"):
            with self.assertRaises(ValueError):
                al.apply_edits(rec, bad)

    def test_reroll_is_stable_keeps_the_persona_and_varies(self):
        rec, _ = al.derive("alpha", "kitchen recipes")
        a = al.reroll(rec, "alpha", 1)
        self.assertEqual(a, al.reroll(rec, "alpha", 1))
        self.assertNotEqual(a, al.reroll(rec, "alpha", 2))
        self.assertNotEqual(a, al.reroll(rec, "other", 1))
        for field in ("hat", "acc", "palette"):                       # set by the persona: kept
            self.assertEqual(a[al.FIELDS.index(field)], rec[al.FIELDS.index(field)])
        self.assertNotIn(al.KEEP, a)                                  # everything else drawn
        seen = {tuple(al.reroll(rec, "alpha", n)) for n in range(1, 60)}
        self.assertGreater(len(seen), 50)

    def test_reroll_draws_within_the_larger_counts_and_reaches_new_parts(self):
        empty = al.empty_record()
        rolls = [al.reroll(empty, "alpha", n) for n in range(1, 400)]
        for field, count in al.COUNTS.items():
            slot = al.FIELDS.index(field)
            values = {r[slot] for r in rolls}
            self.assertTrue(all(0 <= v < count for v in values), field)
            self.assertEqual(max(values), count - 1, field)          # the newest part is reachable

    def test_describe_uses_the_new_words(self):
        rec = al.empty_record()
        for field, name in (("ears", "elf"), ("eyes", "cyclops"), ("mouth", "tongueout"),
                            ("hat", "viking"), ("acc", "eyepatch"), ("pattern", "frost"),
                            ("palette", "Taupe"), ("temper", "curious")):
            rec[al.FIELDS.index(field)] = al._index(field, name)
        text = al.describe(rec)
        for word in ("Taupe", "elf ears", "cyclops eye", "tongue out", "viking helmet",
                     "with an eyepatch", "frosty", "curious"):
            self.assertIn(word, text)

    def test_every_part_has_a_description_word(self):
        for field, words in (("ears", al._EAR_WORDS), ("eyes", al._EYE_WORDS),
                             ("mouth", al._MOUTH_WORDS), ("hat", al._HAT_WORDS),
                             ("acc", al._ACC_WORDS), ("pattern", al._PATTERN_WORDS)):
            for name in al.VOCAB[field]:
                if name != "none":
                    self.assertIn(name, words, field)

    def test_held_items_say_holding_and_worn_ones_say_with(self):
        for name, verb in (("book", "holding"), ("magnifier", "holding"),
                           ("lollipop", "holding"), ("balloon", "holding"),
                           ("monocle", "with"), ("eyepatch", "with"), ("necktie", "with"),
                           ("headset", "with"), ("plaster", "with")):
            rec = al.empty_record()
            rec[al.FIELDS.index("acc")] = al._index("acc", name)
            self.assertIn(f"{verb} {al._ACC_WORDS[name]}", al.describe(rec), name)

    def test_descriptions_name_traits_not_persona_text(self):
        secret = "Zebrafish-Quartz protocol"
        rec, fired = al.derive("alpha", f"{secret}. You are a cook in a happy home kitchen.")
        text = al.describe(rec) + " " + " ".join(fired)
        self.assertNotIn("zebrafish", text.lower())
        self.assertNotIn("quartz", text.lower())
        self.assertRegex(al.describe(rec), r"^an? \w+ character")
        self.assertIn("toque", al.describe(rec))


class PairFlow(unittest.TestCase):
    """pair.py's look step, with the owner's answers scripted."""

    SOUL = "Zebrafish-Quartz protocol. You are a cheerful kitchen assistant."

    def run_choose(self, args, answers=()):
        import argparse
        import contextlib
        import io
        import pair
        ns = argparse.Namespace(look=None, yes=False)
        for k, v in args.items():
            setattr(ns, k, v)
        feed = iter(answers)
        out = io.StringIO()
        with mock.patch("builtins.input", lambda prompt="": next(feed)), \
                contextlib.redirect_stdout(out):
            result = pair.choose_look(ns, "alpha", self.SOUL)
        return result, out.getvalue()

    def test_yes_keeps_the_device_character_unless_asked(self):
        self.assertEqual(self.run_choose({"yes": True}), (None, ""))
        result, text = self.run_choose({"yes": True, "look": "auto"})
        self.assertEqual(len(result), 24)
        self.assertIn("toque", text)

    def test_accept_reroll_keep(self):
        rec, _ = al.derive("alpha", self.SOUL)
        result, text = self.run_choose({}, ["a"])
        self.assertEqual(result, al.to_hex(rec))
        result, text = self.run_choose({}, ["r", "r", "a"])
        self.assertEqual(result, al.to_hex(al.reroll(rec, "alpha", 2)))
        self.assertIn("reroll 2", text)
        self.assertEqual(self.run_choose({}, ["k"])[0], "")

    def test_edit_spec_and_none(self):
        result, _ = self.run_choose({"look": "hat=beanie"})
        self.assertEqual(al.from_hex(result)[al.FIELDS.index("hat")], al.HATS.index("beanie"))
        self.assertEqual(self.run_choose({"look": "none"})[0], "")

    def test_the_persona_is_never_printed(self):
        for args, answers in (({"look": "auto"}, ()), ({}, ["r", "a"])):
            _, text = self.run_choose(args, answers)
            self.assertNotIn("zebrafish", text.lower())
            self.assertNotIn("quartz", text.lower())


if __name__ == "__main__":
    unittest.main(verbosity=1)
