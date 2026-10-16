#!/usr/bin/env bash
# Secret guard — wired as .git/hooks/pre-commit, pre-merge-commit AND
# commit-msg (host/setup.sh links all three):
#   ln -sf ../../host/check-secrets.sh .git/hooks/pre-commit
#   ln -sf ../../host/check-secrets.sh .git/hooks/pre-merge-commit
#   ln -sf ../../host/check-secrets.sh .git/hooks/commit-msg
# Modes:
#   no arguments          hook mode (pre-commit / pre-merge-commit): scan the
#                         STAGED content (index blobs), not the working tree,
#                         so partial staging and deleted working copies can't
#                         slip past. As a commit-msg hook git passes the
#                         message file as $1; that is treated as --message.
#   --message FILE        scan a commit-message file (content rules only).
#   --scan-tree DIR       publication scan: every regular file under DIR
#                         (skipping .git directories), filename and content
#                         rules; runs outside any git repository.
#   --scan-artifact FILE  release-artifact scan: one file read as text
#                         (grep -a): known-private literals, private IPv4
#                         (no path exemptions), profile ids and PEM
#                         private-key blocks.
# Known-private literals are derived at run time from the device settings
# store (gitignored firmware/device.env of the checkout containing this
# script — resolved through symlinks — and in hook mode also the main
# worktree's copy when committing from a git worktree), this machine's
# hostname and the owner's Hermes profile ids (directory names under
# ~/.hermes/profiles) — never from a second copy on disk. They reach grep
# only through pattern files, never its command line (argv is readable by
# every local user). Output is line numbers + rule names only: agents run
# commits and their tool output lands in transcripts, so this hook must
# NEVER print matched content. This script must NEVER contain real secrets
# itself.
set -u

mode=hook
target=""
case "${1:-}" in
    "") ;;
    --message|--scan-tree|--scan-artifact)
        arg="$1"
        mode="${arg#--}"
        mode="${mode#scan-}"
        target="${2:-}"
        [ -n "$target" ] || { echo "BLOCKED: $arg needs a path" >&2; exit 1; }
        ;;
    -*)
        echo "BLOCKED: unknown option: $1 (usage: [--message FILE | --scan-tree DIR | --scan-artifact FILE])" >&2
        exit 1
        ;;
    *)                      # commit-msg hook: git passes the message file
        mode=message
        target="$1"
        ;;
esac
if [ "$mode" != hook ] && [ ! -e "$target" ]; then
    echo "BLOCKED: no such path: $target" >&2
    exit 1
fi

if [ "$mode" = hook ]; then
    cd "$(git rev-parse --show-toplevel)" || exit 1

    # Commit timestamps carry the local UTC offset, which leaks the owner's
    # region. Git takes it from the same environment this hook runs in.
    if [ "$(date +%z)" != "+0000" ]; then
        echo "BLOCKED: commit in UTC so the timestamp doesn't leak your timezone:"
        echo "    TZ=UTC git commit ..."
        echo ""
        echo "Secret guard fired. Device settings belong in firmware/device.env (gitignored) or on the device."
        exit 1
    fi
fi

umask 077
tmp="$(mktemp -d)" || { echo "BLOCKED: could not create scan workspace" >&2; exit 1; }
trap 'rm -rf "$tmp"' EXIT
blob="$tmp/blob"

# A matcher error must survive command substitutions and pipelines. Record
# it in the private workspace and check it before returning from any mode.
# Suppress tool diagnostics: they may include patterns or matched content.
checked_grep() {
    local status
    command grep "$@" 2>/dev/null
    status=$?
    case "$status" in
        0|1) return "$status" ;;
        *) : > "$tmp/matcher-error"; return "$status" ;;
    esac
}

# Known-private literals, one per line, split by how they are matched:
#   words   word-shaped device.env values and the hostname: whole-word, so
#           ordinary words containing them pass (grep -wF)
#   other   anything else (passwords, keys, URLs): substring (grep -F)
#   ids     Hermes profile ids: whole word, any case (grep -iwF); a profile id
#           is also a product word, so only the whole word counts
# Secret-bearing device.env fields only (the profile name there is not
# private and matches ordinary code); for host:port values the host is listed
# on its own too. Placeholders and short values are skipped.
: > "$tmp/words"; : > "$tmp/other"; : > "$tmp/ids"
self="$(readlink -f "${BASH_SOURCE[0]}" 2>/dev/null || printf '%s' "${BASH_SOURCE[0]}")"
script_root="$(cd "$(dirname "$self")/.." && pwd)"
collect_env() {              # $1 = a firmware/device.env; prints raw values
    [ -f "$1" ] || return 0
    sed -n -E 's/^[[:space:]]*(export[[:space:]]+)?(ssid|pass|key|hermes|shim)[[:space:]]*=[[:space:]]*//p' "$1" |
        while IFS= read -r v; do
            v="${v%$'\r'}"                       # CRLF files
            v="${v%%[[:space:]]#*}"               # trailing comment (needs a space before #)
            v="${v#"${v%%[![:space:]]*}"}"        # trim edges: the literal is the
            v="${v%"${v##*[![:space:]]}"}"        # value itself, whitespace-free
            v="${v%\"}"; v="${v#\"}"; v="${v%\'}"; v="${v#\'}"
            v="${v#"${v%%[![:space:]]*}"}"; v="${v%"${v##*[![:space:]]}"}"
            printf '%s\n%s\n' "$v" "${v%%:*}"
        done
}
{
    hostname 2>/dev/null
    collect_env "$script_root/firmware/device.env"
    if [ "$mode" = hook ]; then
        main_tree="$(cd "$(git rev-parse --git-common-dir)/.." && pwd)"
        [ "$main_tree" = "$script_root" ] || collect_env "$main_tree/firmware/device.env"
    fi
} | while IFS= read -r lit; do
    case "${lit,,}" in ''|your-*|your_*|\**) continue ;; esac
    [ "${#lit}" -ge 4 ] || continue
    if [[ "$lit" =~ ^[A-Za-z0-9_-]+$ ]]; then printf '%s\n' "$lit" >> "$tmp/words"
    else printf '%s\n' "$lit" >> "$tmp/other"; fi
done
for d in "${HOME:-/nonexistent}"/.hermes/profiles/*/; do
    [ -d "$d" ] || continue
    id="$(basename "$d")"
    [ "${#id}" -ge 3 ] && [[ "$id" =~ ^[A-Za-z0-9_-]+$ ]] && printf '%s\n' "$id" >> "$tmp/ids"
done
sort -u -o "$tmp/words" "$tmp/words"
sort -u -o "$tmp/other" "$tmp/other"
sort -u -o "$tmp/ids" "$tmp/ids"

# Line numbers of matches, comma-separated (never the matched text).
lines() { cut -d: -f1 | sort -nu | paste -sd, -; }

# Reviewed exceptions (host/guard-exceptions): path <TAB> rule, rule "*"
# meaning every rule. For the synthetic test fixtures whose values are
# deliberately credential-shaped; nothing else belongs there.
GUARDEXC="$script_root/host/guard-exceptions"
excepted() {                # $1 = path, $2 = rule name; 0 = skip reporting
    [ -f "$GUARDEXC" ] || return 1
    local xpath xrule
    while IFS=$'\t' read -r xpath xrule; do
        case "$xpath" in ''|'#'*) continue ;; esac
        if [ "$xpath" = "$1" ] && { [ "$xrule" = "$2" ] || [ "$xrule" = "*" ]; }; then
            return 0
        fi
    done < "$GUARDEXC"
    return 1
}

rc=0

# Secret-shaped filenames: blocked unless a sample/template suffix says the
# file exists to be copied and filled in.
filename_rule() {            # $1 = path as reported; returns 0 = blocked
    excepted "$1" "secret file" && return 1
    local base="${1##*/}"
    case "$base" in
        *.example|*.template|*.sample) return 1 ;;
        .env|*.env|*.env*|*.pem|*.key)
            echo "BLOCKED: secret file: $1"
            return 0
            ;;
    esac
    return 1
}

# Assignment key names, whole-token only: a member access (in.pass, s.key)
# is not an assignment to a credential key. '-' stays a boundary so CLI
# flags like --pass= still count.
KEYRE="(^|[^A-Za-z0-9_.])[\"']?[[:space:]]*(password|passwd|pass|passphrase|secret|api[_-]?key|api_secret|client_secret|token|access_token|auth_token|private_key)[\"']?"
# Value run for unquoted assignments: a credential is a solid token; a run
# with call/sequence punctuation is code, not a secret. The accepted
# residual: a credential containing parentheses slips through this gap,
# which is what keeps ordinary function-call values allowed.
VALRUN='[^[:space:]"(),;{}]'
# Extract unquoted values with ERE matching, without requiring PCRE. Keep
# the separator as a boundary when looking for the next assignment, and
# stop each value before that assignment (including '&', ':' or '=' joins).
# Colons inside URL values remain part of the value.
unquoted_values() {
    awk -v key="${KEYRE}[[:space:]]*[:=]" -v run="^${VALRUN}+" '
        {
            rest = $0
            while (match(tolower(rest), key)) {
                rest = substr(rest, RSTART + RLENGTH - 1)
                value = substr(rest, 2)
                if (match(tolower(rest), key))
                    value = substr(value, 1, RSTART > 1 ? RSTART - 2 : 0)
                sub(/^[[:space:]]*/, "", value)
                if (match(value, run) && RLENGTH >= 8)
                    print NR ":" substr(value, 1, RLENGTH)
            }
        }
    ' < "$1" 2>/dev/null || : > "$tmp/matcher-error"
}
# Extract each assignment, then strip its key and boundary to obtain
# "N:value". Placeholder prefixes exempt only that value, so joined fields
# keep separate decisions in either order. Matched text never reaches
# diagnostics or argv.
ASSIGN_PREFIX="^([0-9]+:)[^A-Za-z0-9_]*[A-Za-z0-9_-]+[\"']?[[:space:]]*[:=][[:space:]]*"
PHVAL='^[0-9]+:[[:space:]]*(your[-_ ]|placeholder|changeme|sample_|example|xxx)|^[0-9]+:[[:space:]]*[x*]+$|^[0-9]+:[[:space:]]*[$]\{[^}]*\}$|^[0-9]+:[[:space:]]*<[^>]*>$'

# Content rules over one file, read as text (-a: a NUL must not disable
# the rules). $1 = label to report (path or "commit message"), $2 = file,
# $3 = 1 to exempt firmware/lib/ paths from the private-IPv4 rule.
scan_text() {
    local label="$1" file="$2" libexempt="$3"
    local found=""
    report() { [ -n "$2" ] && ! excepted "$label" "$1" && found="$found  $1: line(s) $2"$'\n'; }

    # Generic secret assignments, quoted (4+ chars, either quote style) or
    # — when long and token-shaped — unquoted: '=' or ':' separated, key
    # case-insensitive, optionally JSON-quoted. An unquoted value counts
    # only when it starts with '/' at the separator, contains '://', or
    # holds both a letter and a digit; the shape tests run on the extracted
    # value only.
    local coarse qvalues unquoted
    coarse="$(unquoted_values "$file")"
    qvalues="$(checked_grep -anioE "${KEYRE}[[:space:]]*[:=][[:space:]]*(\"[^\"]{4,}\"|'[^']{4,}')" "$file" |
        sed -E "s/${ASSIGN_PREFIX}(\"([^\"]*)\"|'([^']*)').*/\1\3\4/")"
    unquoted="$(printf '%s\n' "$coarse" |
        checked_grep -aE '^[0-9]+:/|://|^[0-9]+:'"${VALRUN}"'*[0-9]' |
        checked_grep -aE '^[0-9]+:/|://|^[0-9]+:'"${VALRUN}"'*[A-Za-z]')"
    report "credential assignment" "$(
        {
            [ -n "$qvalues" ] && printf '%s\n' "$qvalues"
            [ -n "$unquoted" ] && printf '%s\n' "$unquoted"
        } | checked_grep -aviE "$PHVAL" | lines)"

    # Long lowercase-hex literals (token-shaped), either quote style.
    report "hex token" "$(checked_grep -anE "(\"[0-9a-f]{40,}\"|'[0-9a-f]{40,}')" "$file" | lines)"


    # Private-range IPv4 (LAN addresses are device settings, never tracked).
    # Vendored third-party libraries are exempt: their examples use sample IPs.
    if [ "$libexempt" != 1 ]; then
        report "private IPv4" "$(checked_grep -anE '\b(10\.[0-9]{1,3}|172\.(1[6-9]|2[0-9]|3[01])|192\.168)\.[0-9]{1,3}\.[0-9]{1,3}\b' "$file" | lines)"
    fi

    # Known-private literals, from pattern files (see above). An empty pattern
    # file matches nothing.
    lit_lines=""
    [ -s "$tmp/words" ] && lit_lines="$lit_lines$(checked_grep -anwF -f "$tmp/words" "$file")"$'\n'
    [ -s "$tmp/other" ] && lit_lines="$lit_lines$(checked_grep -anF -f "$tmp/other" "$file")"$'\n'
    report "known private value (device.env / hostname)" "$(printf '%s' "$lit_lines" | checked_grep -av '^\$' | lines)"
    if [ -s "$tmp/ids" ]; then
        report "Hermes profile id" "$(checked_grep -aniwF -f "$tmp/ids" "$file" | lines)"
    fi

    # PEM private-key blocks, whatever the surrounding file is named. The
    # header shape is a real one, so this pattern does not match its own
    # source line here.
    report "private key block" "$(checked_grep -anE -- '-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----' "$file" | lines)"

    if [ -n "$found" ]; then
        echo "BLOCKED: secret or private content in $label:"
        printf '%s' "$found"
        rc=1
    fi
}

case "$mode" in
message)
    scan_text "commit message" "$target" 0
    ;;
artifact)
    # A release artifact is scanned as text (-a): only whole-value rules;
    # filenames, assignment shapes and hex strings are source-tree rules.
    found=""
    report() { [ -n "$2" ] && ! excepted "$target" "$1" && found="$found  $1: line(s) $2"$'\n'; }
    a_lines() { cut -d: -f1 | sort -nu | paste -sd, -; }
    lit_lines=""
    [ -s "$tmp/words" ] && lit_lines="$lit_lines$(checked_grep -anwF -f "$tmp/words" "$target")"$'\n'
    [ -s "$tmp/other" ] && lit_lines="$lit_lines$(checked_grep -anF -f "$tmp/other" "$target")"$'\n'
    report "known private value (device.env / hostname)" "$(printf '%s' "$lit_lines" | checked_grep -av '^\$' | a_lines)"
    if [ -s "$tmp/ids" ]; then
        report "Hermes profile id" "$(checked_grep -aniwF -f "$tmp/ids" "$target" | a_lines)"
    fi
    report "private IPv4" "$(checked_grep -anE '\b(10\.[0-9]{1,3}|172\.(1[6-9]|2[0-9]|3[01])|192\.168)\.[0-9]{1,3}\.[0-9]{1,3}\b' "$target" | a_lines)"
    # A PEM block needs a base64 body: framework code carries bare header
    # marker strings for parsing, with no key material after them (-z so a
    # newline between header and body still counts; matched with -q and a
    # synthesized position, because a -z match would print the whole record).
    report "private key block" "$(checked_grep -azqE -- '-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----[[:space:]]*[A-Za-z0-9+/=]{16,}' "$target" && echo 1)"
    if [ -n "$found" ]; then
        echo "BLOCKED: secret or private content in $target:"
        printf '%s' "$found"
        rc=1
    fi
    ;;
tree)
    while IFS= read -r -d '' f; do
        rel="${f#"$target"/}"
        if filename_rule "$rel"; then rc=1; continue; fi
        case "$rel" in firmware/lib/*) scan_text "$rel" "$f" 1 ;; *) scan_text "$rel" "$f" 0 ;; esac
    done < <(find "$target" -type d -name .git -prune -o -type f -print0)
    ;;
hook)
    # NUL-separated names: core.quotepath would otherwise quote unusual paths
    # and make `git show ":$f"` miss them silently.
    while IFS= read -r -d '' f; do
        if filename_rule "$f"; then rc=1; continue; fi

        # The blob exactly as it will be committed (honors GIT_INDEX_FILE,
        # which `git commit -- <path>` points at a temporary index).
        if ! git show ":$f" > "$blob" 2>/dev/null; then
            echo "BLOCKED: could not read staged content of $f"
            rc=1
            continue
        fi
        case "$f" in firmware/lib/*) scan_text "$f" "$blob" 1 ;; *) scan_text "$f" "$blob" 0 ;; esac
    done < <(git diff --cached --name-only -z --diff-filter=ACMR)
    ;;
esac

if [ -e "$tmp/matcher-error" ]; then
    echo "BLOCKED: content matcher failed" >&2
    rc=1
fi

if [ "$rc" -ne 0 ] && [ "$mode" = hook ]; then
    echo ""
    echo "Secret guard fired. Device settings belong in firmware/device.env (gitignored) or on the device."
    echo "Override (say why out loud first): git commit --no-verify"
fi
exit $rc
