#!/usr/bin/env bash
# Bootstrap the workstation side: venv, pinned deps, models, git hooks,
# and (optionally) the systemd user service.
#
#   host/setup.sh                    # venv + deps + models + secret hook
#   host/setup.sh --install-service  # ...and install/start hermes-audio-shim
#
# Idempotent: safe to re-run. Needs Python 3.10-3.12 (misaki has no 3.13+
# wheels) and ffmpeg is NOT required (MP3 encoding is lameenc).
set -euo pipefail

HOST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HOST_DIR/.." && pwd)"
VENV="$HOST_DIR/.venv-tts"
INSTALL_SERVICE=0
[ "${1:-}" = "--install-service" ] && INSTALL_SERVICE=1

PARADEE="git+https://github.com/sahilmahendrakar/paradee@9c8b4d7504cbee7e64de2d0341bb690f0b0ab708"
PINS=(lameenc==1.8.4 faster-whisper==1.2.1)

find_python() {
    local c
    for c in python3.12 python3.11 python3.10; do
        command -v "$c" >/dev/null 2>&1 && { command -v "$c"; return; }
    done
    if command -v uv >/dev/null 2>&1; then
        uv python find '>=3.10,<3.13' 2>/dev/null && return
    fi
    for c in "$HOME"/.local/share/uv/python/cpython-3.1[012].*/bin/python3.1[012]; do
        [ -x "$c" ] && { echo "$c"; return; }
    done
    return 1
}

if [ ! -x "$VENV/bin/python" ]; then
    PY="$(find_python)" || {
        echo "error: need Python 3.10-3.12 (try: uv python install 3.11)" >&2
        exit 1
    }
    echo "==> venv: $VENV ($("$PY" --version))"
    "$PY" -m venv "$VENV"
fi
ver="$("$VENV/bin/python" -c 'import sys; print("%d%02d" % sys.version_info[:2])')"
if [ "$ver" -ge 313 ] || [ "$ver" -lt 310 ]; then
    echo "error: $VENV uses an unsupported Python; delete it and re-run" >&2
    exit 1
fi

echo "==> deps"
"$VENV/bin/python" -m pip install -q --upgrade pip
"$VENV/bin/python" -m pip install -q "$PARADEE" "${PINS[@]}"

echo "==> models (Paradee ONNX -> host/models, whisper base -> HF cache)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
printf 'Setup check.' > "$tmp/in.txt"
"$VENV/bin/python" "$HOST_DIR/local_tts.py" --text-file "$tmp/in.txt" \
    --out "$tmp/out.mp3" >/dev/null
"$VENV/bin/python" -c "from faster_whisper import WhisperModel; \
WhisperModel('base', device='cpu', compute_type='int8')"

echo "==> git secret guard (pre-commit, pre-merge-commit and commit-msg)"
for hook in pre-commit pre-merge-commit commit-msg; do
    ln -sf ../../host/check-secrets.sh "$REPO/.git/hooks/$hook"
done

if [ "$INSTALL_SERVICE" = 1 ]; then
    unit="$HOME/.config/systemd/user/hermes-audio-shim.service"
    echo "==> systemd user unit: $unit"
    mkdir -p "$(dirname "$unit")"
    cat > "$unit" <<EOF
[Unit]
Description=Hermes audio shim (STT/TTS for the T-Embed voice device)
After=network-online.target

[Service]
ExecStart=$VENV/bin/python $HOST_DIR/audio_shim.py
Restart=on-failure
RestartSec=5
Environment=SHIM_WHISPER_MODEL=base

[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload
    systemctl --user enable --now hermes-audio-shim
    systemctl --user restart hermes-audio-shim
    echo "    $(systemctl --user is-active hermes-audio-shim)"
fi

echo "==> done"
