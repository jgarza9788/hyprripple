#!/bin/bash
# HyprRipple installer / uninstaller. Use ./install and ./uninstall.
#
# Every config edit is fenced (">>> hyprripple >>>" ... "<<< hyprripple <<<"),
# idempotent (running twice changes nothing) and preceded by a backup
# (<file>.bak.hyprripple.<epoch>). --dry-run shows what would happen.
#
# Test hooks (tests/setup.test.sh): HYPRRIPPLE_HYPR_DIR, HYPRRIPPLE_MENU_FILE,
# HYPRRIPPLE_SKIP_SYSTEM=1 (no hyprpm / hyprctl / omarchy commands).
set -euo pipefail

ID="jgarza.hyprripple"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HYPR_DIR="${HYPRRIPPLE_HYPR_DIR:-$HOME/.config/hypr}"
MENU_FILE="${HYPRRIPPLE_MENU_FILE:-$HOME/.config/omarchy/extensions/omarchy-menu.jsonc}"
STATE_DIR="$HOME/.config/omarchy/$ID"
PLUGINS_DIR="$HOME/.config/omarchy/plugins"
SKIP_SYSTEM="${HYPRRIPPLE_SKIP_SYSTEM:-0}"

DRY_RUN=0
NO_MENU=0
PURGE=0

if [[ -t 1 ]]; then B=$'\e[1m'; G=$'\e[32m'; Y=$'\e[33m'; R=$'\e[31m'; N=$'\e[0m'; else B= G= Y= R= N=; fi
step() { printf '%s==>%s %s\n' "$B" "$N" "$*"; }
ok()   { printf '    %s✓%s %s\n' "$G" "$N" "$*"; }
skip() { printf '    %s-%s %s\n' "$Y" "$N" "$*"; }
warn() { printf '    %s!%s %s\n' "$Y" "$N" "$*" >&2; }
die()  { printf '%serror:%s %s\n' "$R" "$N" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# run CMD... unless --dry-run
run() {
  if (( DRY_RUN )); then skip "would run: $*"; return 0; fi
  "$@"
}

backup() {
  local f="$1"
  [[ -f $f ]] || return 0
  (( DRY_RUN )) && return 0
  cp -p -- "$f" "$f.bak.hyprripple.$(date +%s)"
}

# ── config edits (python: exact, comment-aware, validated) ───────────────────
# edit_lua install|uninstall FILE WRITE(0|1) -> prints "changed" or "unchanged"
# (WRITE=0 only reports what would change)
edit_lua() {
  python3 - "$1" "$2" "$3" <<'PY'
import re, sys
mode, path, write = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
BEGIN, END = "-- >>> hyprripple >>>", "-- <<< hyprripple <<<"
BLOCK = [BEGIN,
         "-- HyprRipple: Material ink ripples. Settings: Omarchy menu > Style > HyprRipple",
         'require("hypr.hyprripple")',
         END]
LEGACY_COMMENT = re.compile(r'^\s*--\s*HyprRipple \(Material ink ripples\);.*$')
REQUIRE = re.compile(r'^\s*require\(\s*["\']hypr\.hyprripple["\']\s*\)\s*$')
text = open(path).read()
lines = text.split("\n")
out = lines
if mode == "install":
    if any(l.strip() == BEGIN for l in lines) or any(REQUIRE.match(l) for l in lines):
        print("unchanged"); sys.exit(0)
    at = next((i + 1 for i, l in enumerate(lines) if re.match(r'^\s*require\(\s*["\']hypr\.plugins["\']\s*\)', l)), None)
    if at is None:  # append at the end, keeping a single trailing newline
        while out and out[-1] == "":
            out = out[:-1]
        out = out + [""] + BLOCK + [""]
    else:
        out = lines[:at] + BLOCK + lines[at:]
else:
    out, skipping = [], False
    for i, l in enumerate(lines):
        if l.strip() == BEGIN:
            skipping = True
            if out and out[-1].strip() == "" and i + 1 < len(lines):  # blank added on append
                nxt = lines[i + 1:]
                if all(x.strip() in ("", END) or x.strip() in BLOCK for x in nxt):
                    out.pop()
            continue
        if skipping:
            if l.strip() == END: skipping = False
            continue
        if REQUIRE.match(l): continue
        if LEGACY_COMMENT.match(l) and i + 1 < len(lines) and REQUIRE.match(lines[i + 1]): continue
        out.append(l)
new = "\n".join(out)
if new == text:
    print("unchanged"); sys.exit(0)
if write:
    open(path, "w").write(new)
print("changed")
PY
}

# edit_menu install|uninstall FILE WRITE(0|1) -> "changed" / "unchanged";
# refuses to write anything that isn't valid JSON once comments are stripped.
edit_menu() {
  python3 - "$1" "$2" "$3" <<'PY'
import json, re, sys
mode, path, write = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
BEGIN, END = "// >>> hyprripple >>>", "// <<< hyprripple <<<"
ACTION = "omarchy-shell shell toggle jgarza.hyprripple '{}'"
ENTRIES = [
    '"hyprripple": {"icon":"󰐊","label":"HyprRipple","aliases":["hyprripple","ripple","ink","material ripple","click effect"],'
    '"description":"Material ink ripple settings: triggers, preset, color","action":"' + ACTION + '"}',
    '"style.hyprripple": {"icon":"󰐊","label":"HyprRipple","description":"Material ink ripple settings: triggers, preset, color",'
    '"action":"' + ACTION + '"}',
]
KEY = re.compile(r'^\s*"(style\.)?hyprripple"\s*:')

def strip_comments(t):
    out, i, n, in_str = [], 0, len(t), False
    while i < n:
        c = t[i]
        if in_str:
            out.append(c)
            if c == "\\": out.append(t[i + 1]); i += 2; continue
            if c == '"': in_str = False
        elif c == '"': in_str = True; out.append(c)
        elif t.startswith("//", i):
            while i < n and t[i] != "\n": i += 1
            continue
        elif t.startswith("/*", i):
            j = t.find("*/", i + 2); i = n if j < 0 else j + 2; continue
        else: out.append(c)
        i += 1
    return "".join(out)

def valid(t):
    try: return isinstance(json.loads(strip_comments(t)), dict)
    except Exception: return False

def code_part(line):  # line without a trailing // comment (outside strings)
    return strip_comments(line).rstrip()

text = open(path).read()
if not valid(text):
    print("invalid"); sys.exit(0)
lines = text.split("\n")
close = max(i for i, l in enumerate(lines) if code_part(l).strip() == "}")

if mode == "install":
    if any(KEY.match(l) for l in lines):
        print("unchanged"); sys.exit(0)
    prev = next((i for i in range(close - 1, -1, -1) if code_part(lines[i]).strip() not in ("", "{")), None)
    if prev is not None and not code_part(lines[prev]).endswith(","):
        cp = code_part(lines[prev])
        lines[prev] = cp + "," + lines[prev][len(cp):]
    block = ["", "  " + BEGIN] + ["  " + e + ("," if k < len(ENTRIES) - 1 else "") for k, e in enumerate(ENTRIES)] + ["  " + END]
    lines = lines[:close] + block + lines[close:]
else:
    out, skipping = [], False
    for l in lines:
        s = l.strip()
        if s == BEGIN:
            skipping = True
            if out and out[-1].strip() == "":  # the blank line install added
                out.pop()
            continue
        if skipping:
            if s == END: skipping = False
            continue
        if KEY.match(l): continue
        out.append(l)
    lines = out
    # drop a now-trailing comma before the closing brace
    close = max(i for i, l in enumerate(lines) if code_part(l).strip() == "}")
    prev = next((i for i in range(close - 1, -1, -1) if code_part(lines[i]).strip() != ""), None)
    if prev is not None and code_part(lines[prev]).endswith(","):
        cp = code_part(lines[prev])
        lines[prev] = cp[:-1] + lines[prev][len(cp):]
    # collapse blank lines left behind before the brace
    while close >= 2 and lines[close - 1].strip() == "" and lines[close - 2].strip() == "":
        del lines[close - 1]; close -= 1

new = "\n".join(lines)
if new == text:
    print("unchanged"); sys.exit(0)
if not valid(new):
    print("invalid-result"); sys.exit(0)
if write:
    open(path, "w").write(new)
print("changed")
PY
}

# apply EDITFN MODE FILE: probe without writing, back up, then write.
apply() {
  local res
  res=$("$1" "$2" "$3" 0)
  if [[ $res == changed ]] && (( ! DRY_RUN )); then
    backup "$3"
    res=$("$1" "$2" "$3" 1)
  fi
  printf '%s' "$res"
}

report() { # report RESULT FILE WHAT
  case "$1" in
    changed)        (( DRY_RUN )) && skip "would update $2 ($3)" || ok "updated $2 ($3)" ;;
    unchanged)      skip "$2 already ${4:-up to date}" ;;
    invalid)        warn "$2 isn't valid JSON(C); left it alone - add the menu entries by hand (see README)" ;;
    invalid-result) warn "editing $2 would break it; left it alone - add the menu entries by hand (see README)" ;;
  esac
}

# ── install ──────────────────────────────────────────────────────────────────
do_install() {
  step "Checking requirements"
  have python3 || die "python3 is needed for the config edits"
  [[ -f $HYPR_DIR/hyprland.lua ]] || die "no $HYPR_DIR/hyprland.lua - HyprRipple needs Hyprland's Lua config (Hyprland 0.56+)"
  local omarchy=0
  if (( ! SKIP_SYSTEM )); then
    for c in hyprctl hyprpm make g++ pkg-config git; do have "$c" || die "missing '$c'"; done
    git -C "$REPO_DIR" rev-parse HEAD >/dev/null 2>&1 || die "$REPO_DIR isn't a git checkout - hyprpm builds from git; clone it instead of downloading a zip"
    have omarchy-plugin-enable && omarchy=1
  fi
  ok "ok"

  if (( ! SKIP_SYSTEM )); then
    step "Native Hyprland plugin (hyprpm builds it; it may ask for your password)"
    # never keep a dev build loaded alongside the hyprpm one
    (( DRY_RUN )) || hyprctl plugin unload "$REPO_DIR/hyprripple.so" >/dev/null 2>&1 || true
    if hyprpm list 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | grep -q "Plugin hyprripple"; then
      run hyprpm update
    else
      run hyprpm update
      if (( DRY_RUN )); then skip "would run: hyprpm add $REPO_DIR"; else printf 'y\n' | hyprpm add "$REPO_DIR"; fi
    fi
    run hyprpm enable hyprripple
    run hyprpm reload -n
  fi

  step "Settings file"
  if [[ -f $HYPR_DIR/hyprripple.lua ]]; then
    skip "$HYPR_DIR/hyprripple.lua exists - keeping your settings"
  else
    run cp -- "$REPO_DIR/lua/hyprripple.lua" "$HYPR_DIR/hyprripple.lua"
    (( DRY_RUN )) || ok "created $HYPR_DIR/hyprripple.lua"
  fi

  step "Load it from hyprland.lua"
  local res
  res=$(apply edit_lua install "$HYPR_DIR/hyprland.lua")
  report "$res" "$HYPR_DIR/hyprland.lua" "require(\"hypr.hyprripple\")" "loads it"

  if (( ! NO_MENU )); then
    step "Omarchy menu entries"
    if [[ -f $MENU_FILE ]]; then
      res=$(apply edit_menu install "$MENU_FILE")
      report "$res" "$MENU_FILE" "HyprRipple + Style > HyprRipple" "has them"
    else
      skip "no $MENU_FILE (not Omarchy, or menu extensions unused) - skipped"
    fi
  fi

  if (( omarchy )); then
    step "Omarchy shell plugin (settings panel + background service)"
    if [[ $REPO_DIR != "$PLUGINS_DIR/$ID" ]]; then
      warn "this checkout isn't at $PLUGINS_DIR/$ID, so Omarchy won't find the panel."
      warn "Install with: omarchy plugin add <repo-url> --enable   (then run ./install from there)"
    else
      run omarchy-shell -q shell rescanPlugins
      if (( DRY_RUN )); then skip "would run: omarchy-plugin-enable $ID"; else omarchy-plugin-enable "$ID" >/dev/null 2>&1 && ok "enabled $ID" || warn "couldn't enable $ID - run: omarchy plugin enable $ID"; fi
    fi
  fi

  if (( ! SKIP_SYSTEM && ! DRY_RUN )); then
    step "Checking"
    hyprctl reload >/dev/null 2>&1 || true
    sleep 1
    if hyprctl hyprripple get 2>/dev/null | grep -q '"enabled"'; then
      ok "HyprRipple is running. Click something!"
      have omarchy-plugin-enable && ok "Settings: Omarchy menu > Style > HyprRipple (or search \"ripple\")"
    else
      warn "the plugin didn't load; check: hyprpm list   and   hyprctl plugin list"
    fi
  fi
}

# ── uninstall ────────────────────────────────────────────────────────────────
do_uninstall() {
  have python3 || die "python3 is needed for the config edits"
  local res

  step "hyprland.lua"
  if [[ -f $HYPR_DIR/hyprland.lua ]]; then
    res=$(apply edit_lua uninstall "$HYPR_DIR/hyprland.lua")
    report "$res" "$HYPR_DIR/hyprland.lua" "removed the require" "has no HyprRipple lines"
  fi

  step "Omarchy menu entries"
  if [[ -f $MENU_FILE ]]; then
    res=$(apply edit_menu uninstall "$MENU_FILE")
    report "$res" "$MENU_FILE" "removed HyprRipple entries" "has no HyprRipple entries"
  else
    skip "no $MENU_FILE"
  fi

  if (( ! SKIP_SYSTEM )); then
    step "Shell plugin"
    if have omarchy-plugin-disable; then
      if (( DRY_RUN )); then skip "would run: omarchy-plugin-disable $ID"; else omarchy-plugin-disable "$ID" >/dev/null 2>&1 && ok "disabled $ID" || skip "$ID wasn't enabled"; fi
    fi
    step "Native plugin"
    (( DRY_RUN )) || hyprctl plugin unload "$REPO_DIR/hyprripple.so" >/dev/null 2>&1 || true
    if hyprpm list 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | grep -q "Plugin hyprripple"; then
      run hyprpm disable hyprripple
      if (( DRY_RUN )); then skip "would run: hyprpm remove hyprripple"; else hyprpm remove hyprripple || warn "hyprpm remove failed; run it yourself: hyprpm remove hyprripple"; fi
      run hyprpm reload -n
    else
      skip "not installed in hyprpm"
    fi
  fi

  step "Settings"
  if (( PURGE )); then
    for f in "$HYPR_DIR/hyprripple.lua" "$STATE_DIR"; do
      [[ -e $f ]] || continue
      run rm -rf -- "$f"
      (( DRY_RUN )) || ok "removed $f"
    done
  else
    skip "kept $HYPR_DIR/hyprripple.lua and $STATE_DIR (--purge removes them)"
  fi
  (( SKIP_SYSTEM || DRY_RUN )) || { hyprctl reload >/dev/null 2>&1 || true; }
  ok "HyprRipple uninstalled"
}

usage() {
  cat <<EOF
Usage: ./install   [--dry-run] [--no-menu]
       ./uninstall [--dry-run] [--purge]

  --dry-run   show what would change, change nothing
  --no-menu   don't add Omarchy menu entries
  --purge     also delete your settings (hyprripple.lua, per-preset memory)
EOF
}

main() {
  local cmd="${1:-}"; shift || true
  for a in "$@"; do
    case "$a" in
      --dry-run) DRY_RUN=1 ;;
      --no-menu) NO_MENU=1 ;;
      --purge)   PURGE=1 ;;
      -h|--help) usage; exit 0 ;;
      *) usage; die "unknown option: $a" ;;
    esac
  done
  (( DRY_RUN )) && printf '%s(dry run - nothing will be changed)%s\n' "$Y" "$N"
  case "$cmd" in
    install)   do_install ;;
    uninstall) do_uninstall ;;
    *) usage; exit 1 ;;
  esac
}

main "$@"
