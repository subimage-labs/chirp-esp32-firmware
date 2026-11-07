# Contributing

Thank you for helping. This page tells you how to send a change.

## License and sign-off

chirp is free software under the GNU General Public License, version 3 only
(GPL-3.0-only). Your contribution uses the same license.

Every commit needs a `Signed-off-by` line. It says that you wrote the change,
or that you have the right to send it under this license (the
[Developer Certificate of Origin](https://developercertificate.org/)). Add it
with `-s`:

```sh
TZ=UTC git commit -s -m "Short summary"
```

Use your real name and an email address you control. Commits without the line
cannot be merged.

## Set up your clone

1. Clone the repository.
2. Turn on the secret guard. It is a git hook (`host/check-secrets.sh`) that
   blocks WiFi credentials, API keys, LAN addresses and private-key blocks in
   commits and in commit messages. Run `host/setup.sh`, or link it yourself:

   ```sh
   for hook in pre-commit pre-merge-commit commit-msg; do
     ln -sf ../../host/check-secrets.sh ".git/hooks/$hook"
   done
   ```

3. Commit with `TZ=UTC git commit` (also for `git merge`). The hook refuses a
   commit that is not made in UTC.

Never bypass the hook with `--no-verify`. If you think it is wrong, open an
issue.

## Rules in short

[AGENTS.md](AGENTS.md) has the full rules. Read it before a larger change.

- **Describe the current state.** Code, comments and docs say what is true now.
  When you replace a path, delete the old one in the same change.
- **No secrets and no private details.** No WiFi credentials, API keys, LAN
  addresses, host names or profile ids in code, docs, tests, commit messages,
  issues or pull requests. Settings live on the device. Use
  `firmware/device.env` (gitignored) for local values.
- **Two lanes.** Device code (`firmware/`) and host code (`host/`) are separate.
  Say in the pull request when a change crosses both.
- **Firmware structure.** One module per concern. Pure logic goes in
  Arduino-free headers with host tests. See `firmware/include/app.h`.
- **Text and layout.** Draw text only through the `ui` font API. Screen
  geometry comes from the pure layout headers and their tests.

## Check your change

Firmware:

```sh
pio run -d firmware -e t-embed-cc1101   # build; also run it with -e t-deck
firmware/test/host/run.sh               # host unit tests (g++, TSan for the concurrent ones)
```

Always name the board with `-e`. Without it, `pio ... -t upload` flashes every
board in turn.

Host (Python):

```sh
python3 -m py_compile host/*.py
python3 -m unittest discover -s host -p 'test_*.py'
```

If you change the tests of the secret guard, also run
`python3 -m unittest discover -s host -p 'test_check_secrets*.py'`.

A change to the device behavior needs a bench build on a real device
(`PLATFORMIO_BUILD_FLAGS=-DBENCH_CMDS`) and `host/smoke.py`. Say in the pull
request what you ran, and on which board. Do not flash a device that someone
is using.

CI (`.github/workflows/ci.yml`) runs the secret scan, the host tests, the
firmware build and the bench build on every push and pull request.

## Send a pull request

1. Make a branch. Keep the change small and focused.
2. Write a test first when the logic can be tested without a device.
3. Run the checks above. Report the commands and their results in the pull
   request.
4. Sign off every commit.

## Report a bug, ask for a board

Use the issue forms: **Bug report** or **New board**. Never paste keys or
passwords. For a security problem, follow [SECURITY.md](SECURITY.md) instead.
