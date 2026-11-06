#!/usr/bin/env bash
# Host-side tests for hardware-independent firmware modules, under TSan.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=thread \
    -I"$fw/include" "$fw/src/turntext.cpp" "$here/turntext_test.cpp" \
    -o "$out/turntext_test" -pthread
"$out/turntext_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -I"$fw/include" \
    "$here/keyring_test.cpp" -o "$out/keyring_test" -pthread
"$out/keyring_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/wheel_test.cpp" -o "$out/wheel_test"
"$out/wheel_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/power_policy_test.cpp" -o "$out/power_policy_test"
"$out/power_policy_test"
g++ -std=c++11 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/battery_test.cpp" -o "$out/battery_test"
"$out/battery_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/sentence_packer_test.cpp" -o "$out/sentence_packer_test"
"$out/sentence_packer_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/gesture_test.cpp" -o "$out/gesture_test"
"$out/gesture_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/route_test.cpp" -o "$out/route_test"
"$out/route_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/keymap_test.cpp" -o "$out/keymap_test"
"$out/keymap_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/setup_form_test.cpp" -o "$out/setup_form_test"
"$out/setup_form_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/wifi_pick_test.cpp" -o "$out/wifi_pick_test"
"$out/wifi_pick_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/portal_layout_test.cpp" -o "$out/portal_layout_test"
"$out/portal_layout_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/ui_layout_test.cpp" -o "$out/ui_layout_test"
"$out/ui_layout_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/balltap_test.cpp" -o "$out/balltap_test"
"$out/balltap_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/venice_policy_test.cpp" -o "$out/venice_policy_test"
"$out/venice_policy_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/venice_voice_test.cpp" -o "$out/venice_voice_test"
"$out/venice_voice_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/venice_selftest_test.cpp" -o "$out/venice_selftest_test"
"$out/venice_selftest_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/selftest_policy_test.cpp" -o "$out/selftest_policy_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/ota_policy_test.cpp" -o "$out/ota_policy_test"
"$out/selftest_policy_test"
"$out/ota_policy_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/theme_test.cpp" -o "$out/theme_test"
"$out/theme_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/font_test.cpp" -o "$out/font_test"
"$out/font_test"
g++ -std=c++11 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/setup_view_test.cpp" -o "$out/setup_view_test"
"$out/setup_view_test"
g++ -std=c++11 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/expiry_test.cpp" -o "$out/expiry_test"
"$out/expiry_test"
g++ -std=c++11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -I"$fw/include" "$here/line_store_test.cpp" \
    -o "$out/line_store_test"
"$out/line_store_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -I"$fw/include" "$here/avatar_test.cpp" \
    -o "$out/avatar_test"
"$out/avatar_test"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -I"$fw/include" "$here/logo_test.cpp" \
    -o "$out/logo_test"
"$out/logo_test"
g++ -std=c++11 -O1 -Wall -Wextra -Werror -I"$fw/include" \
    "$here/logo_preview.cpp" -o "$out/logo_preview"
"$out/logo_preview" frame 0 >/dev/null
"$out/logo_preview" webbg >/dev/null
for art in "$fw"/tools/logo_options/*.txt; do "$out/logo_preview" frame 0 "$art" >/dev/null; done
g++ -std=c++11 -O1 -Wall -Wextra -Werror -I"$fw/include" \
    "$here/mem_source_test.cpp" -o "$out/mem_source_test"
"$out/mem_source_test"
g++ -std=c++11 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/bubbles_test.cpp" -o "$out/bubbles_test"
"$out/bubbles_test"
g++ -std=c++11 -O1 -g -Wall -Wextra -Werror -I"$fw/include" \
    "$here/anchor_test.cpp" -o "$out/anchor_test"
"$out/anchor_test"
g++ -std=c++11 -O1 -Wall -Wextra -Werror -fsyntax-only -I"$fw/include" \
    "$here/avatar_test.cpp"
g++ -std=c++11 -O1 -Wall -Wextra -Werror -I"$fw/include" \
    "$here/avatar_preview.cpp" -o "$out/avatar_preview"
"$out/avatar_preview" stats 500 >/dev/null
PYTHONDONTWRITEBYTECODE=1 python3 -W error "$here/test_logo_web.py"
PYTHONDONTWRITEBYTECODE=1 python3 -W error "$here/test_build_paths.py"
python3 -W error "$here/../../../host/test_avatar_look.py"
python3 -W error "$here/../../../host/test_pair.py"
python3 -W error "$here/../../../host/test_provision.py"
python3 -W error "$here/../../../host/test_publish_firmware.py"
python3 -W error "$here/../../../host/test_audio_shim.py"
python3 -W error "$here/../../../host/test_smoke.py"
