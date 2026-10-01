#!/bin/bash
# Run: bash tests/setup.test.sh   (also part of `make test`)
#
# Exercises ./install and ./uninstall config edits against throwaway files in
# a temp HOME, with hyprpm / hyprctl / omarchy commands switched off.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
failed=0
ok() { if [[ $2 == 0 ]]; then echo "PASS $1"; else echo "FAIL $1"; failed=$((failed + 1)); fi; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export HOME="$TMP/home"
# keep everything the scripts write inside the temp home
export XDG_CACHE_HOME="$HOME/.cache" XDG_CONFIG_HOME="$HOME/.config" XDG_STATE_HOME="$HOME/.local/state"
export HYPRRIPPLE_HYPR_DIR="$HOME/.config/hypr"
export HYPRRIPPLE_MENU_FILE="$HOME/.config/omarchy/extensions/omarchy-menu.jsonc"
export HYPRRIPPLE_SKIP_SYSTEM=1
H="$HYPRRIPPLE_HYPR_DIR"
M="$HYPRRIPPLE_MENU_FILE"

# JSON once // and /* */ comments (outside strings) are stripped
jsonc_valid() {
  python3 - "$1" <<'PY'
import json, sys
t, out, i, s = open(sys.argv[1]).read(), [], 0, False
while i < len(t):
    c = t[i]
    if s:
        out.append(c)
        if c == "\\": out.append(t[i + 1]); i += 2; continue
        if c == '"': s = False
    elif c == '"': s = True; out.append(c)
    elif t.startswith("//", i):
        while i < len(t) and t[i] != "\n": i += 1
        continue
    else: out.append(c)
    i += 1
try: sys.exit(0 if isinstance(json.loads("".join(out)), dict) else 1)
except Exception: sys.exit(1)
PY
}

# same <file> <expected content>: byte-exact (keeps trailing newlines)
same() { cmp -s "$1" <(printf '%s' "$2"); }

fresh() { # fresh <hyprland.lua body> <menu body>
  rm -rf "$HOME"; mkdir -p "$H" "$(dirname "$M")"
  printf '%s' "$1" > "$H/hyprland.lua"
  printf '%s' "$2" > "$M"
}

LUA_STOCK=$'require("hypr.monitors")\nrequire("hypr.plugins")\n\n-- personal stuff\n'
LUA_NOPLUGINS=$'require("hypr.monitors")\n-- end\n'
MENU_EMPTY=$'{\n  // Extend the menu with JSONC.\n  // "personal": {"icon":"","label":"Personal"},\n}\n'
MENU_ENTRIES=$'{\n  // comment\n  "loadout": {"icon":"x","label":"Loadout","action":"a"},\n  "wallsync": {"icon":"y","label":"Wallsync","action":"b"}\n}\n'
MENU_TRAILING=$'{\n  "pinball": {"icon":"z","label":"Pinball","action":"c"} // a game\n}\n'

run_i() { "$ROOT/install" "$@" >/dev/null 2>&1; }
run_u() { "$ROOT/uninstall" "$@" >/dev/null 2>&1; }

# ── install: hyprland.lua ────────────────────────────────────────────────────
fresh "$LUA_STOCK" "$MENU_EMPTY"
run_i; ok "install exits 0" $?
grep -q 'require("hypr.hyprripple")' "$H/hyprland.lua"; ok "require added" $?
awk '/require\("hypr.plugins"\)/{p=NR} /hyprripple >>>/{b=NR} END{exit !(b==p+1)}' "$H/hyprland.lua"; ok "placed right after hypr.plugins" $?
grep -q -- '-- personal stuff' "$H/hyprland.lua"; ok "rest of hyprland.lua kept" $?
[[ -f $H/hyprripple.lua ]]; ok "settings file created" $?
cmp -s "$H/hyprripple.lua" "$ROOT/lua/hyprripple.lua"; ok "settings file is the template" $?
ls "$H"/hyprland.lua.bak.hyprripple.* >/dev/null 2>&1; ok "hyprland.lua backed up" $?
before=$(cat "$H/hyprland.lua" "$M"); run_i
[[ "$(cat "$H/hyprland.lua" "$M")" == "$before" ]]; ok "second install changes nothing" $?
[[ $(grep -c 'require("hypr.hyprripple")' "$H/hyprland.lua") == 1 ]]; ok "no duplicate require" $?

fresh "$LUA_NOPLUGINS" "$MENU_EMPTY"; run_i
tail -n 3 "$H/hyprland.lua" | grep -q 'require("hypr.hyprripple")'; ok "appended when no hypr.plugins" $?

fresh $'require("hypr.plugins")\n-- HyprRipple (Material ink ripples); settings: omarchy-shell shell toggle jgarza.hyprripple\nrequire("hypr.hyprripple")\n' "$MENU_EMPTY"
run_i; [[ $(grep -c 'require("hypr.hyprripple")' "$H/hyprland.lua") == 1 ]]; ok "existing (unfenced) require respected" $?

echo 'mine = true' > "$H/hyprripple.lua"; run_i
grep -q 'mine = true' "$H/hyprripple.lua"; ok "existing settings kept" $?

# ── install: menu ────────────────────────────────────────────────────────────
fresh "$LUA_STOCK" "$MENU_EMPTY"; run_i
jsonc_valid "$M"; ok "menu valid after install (empty menu)" $?
grep -q '"style.hyprripple"' "$M" && grep -q '"hyprripple"' "$M"; ok "menu entries added" $?

fresh "$LUA_STOCK" "$MENU_ENTRIES"; run_i
jsonc_valid "$M"; ok "menu valid after install (existing entries)" $?
grep -q '"loadout"' "$M" && grep -q '"wallsync"' "$M"; ok "existing menu entries kept" $?

fresh "$LUA_STOCK" "$MENU_TRAILING"; run_i
jsonc_valid "$M"; ok "menu valid when last entry has a // comment" $?
grep -q '// a game' "$M"; ok "trailing comment kept" $?

fresh "$LUA_STOCK" "$MENU_ENTRIES"; run_i --no-menu
! grep -q hyprripple "$M"; ok "--no-menu leaves the menu alone" $?

fresh "$LUA_STOCK" $'{ this is not json\n'; run_i
same "$M" $'{ this is not json\n'; ok "broken menu file left untouched" $?

# ── dry run ──────────────────────────────────────────────────────────────────
fresh "$LUA_STOCK" "$MENU_ENTRIES"
b1=$(cat "$H/hyprland.lua"); b2=$(cat "$M")
run_i --dry-run
[[ "$(cat "$H/hyprland.lua")" == "$b1" && "$(cat "$M")" == "$b2" && ! -f $H/hyprripple.lua ]]; ok "--dry-run changes nothing" $?
! ls "$H"/*.bak.hyprripple.* >/dev/null 2>&1; ok "--dry-run makes no backups" $?

# ── uninstall ────────────────────────────────────────────────────────────────
fresh "$LUA_STOCK" "$MENU_ENTRIES"; run_i; run_u; ok "uninstall exits 0" $?
same "$H/hyprland.lua" "$LUA_STOCK"; ok "hyprland.lua restored exactly" $?
jsonc_valid "$M" && ! grep -q hyprripple "$M"; ok "menu entries removed, still valid" $?
same "$M" "$MENU_ENTRIES"; ok "menu restored exactly" $?
[[ ! -f $H/hyprripple.lua ]]; ok "settings removed by default" $?
! ls "$H"/hyprland.lua.bak.hyprripple.* "$M".bak.hyprripple.* >/dev/null 2>&1; ok "installer backups cleaned up" $?
ls "$HOME"/.cache/hyprripple-uninstall-*/hyprland.lua >/dev/null 2>&1; ok "uninstall keeps one safety copy in ~/.cache" $?
! ls "$HOME"/.cache/hyprripple-uninstall-*/*.bak.* >/dev/null 2>&1; ok "safety copy has no stray backups" $?

fresh "$LUA_STOCK" "$MENU_ENTRIES"; run_i; run_u --keep-settings
[[ -f $H/hyprripple.lua ]]; ok "--keep-settings keeps settings" $?
! grep -q hyprripple "$H/hyprland.lua"; ok "--keep-settings still removes the require" $?

fresh "$LUA_STOCK" "$MENU_EMPTY"; run_i; run_u
jsonc_valid "$M"; ok "empty menu valid after round trip" $?

fresh "$LUA_NOPLUGINS" "$MENU_EMPTY"; run_i; run_u
same "$H/hyprland.lua" "$LUA_NOPLUGINS"; ok "appended require round-trips exactly" $?
same "$M" "$MENU_EMPTY"; ok "empty menu round-trips exactly" $?

fresh $'require("hypr.plugins")\n-- HyprRipple (Material ink ripples); settings: omarchy-shell shell toggle jgarza.hyprripple\nrequire("hypr.hyprripple")\n' \
  $'{\n  "loadout": {"a":1},\n  "hyprripple": {"label":"HyprRipple"},\n  "style.hyprripple": {"label":"HyprRipple"},\n  "pinball": {"b":2}\n}\n'
run_u
! grep -q hyprripple "$H/hyprland.lua"; ok "legacy unfenced require + comment removed" $?
jsonc_valid "$M" && ! grep -q hyprripple "$M" && grep -q pinball "$M"; ok "legacy unfenced menu entries removed" $?

fresh "$LUA_STOCK" "$MENU_EMPTY"; run_i; mkdir -p "$HOME/.config/omarchy/jgarza.hyprripple"; echo '{}' > "$HOME/.config/omarchy/jgarza.hyprripple/presets.json"; run_u
[[ ! -f $H/hyprripple.lua && ! -d $HOME/.config/omarchy/jgarza.hyprripple ]]; ok "per-preset memory removed by default" $?
"$ROOT/uninstall" | grep -q "completely gone"; ok "reports nothing left" $?
fresh "$LUA_STOCK" "$MENU_EMPTY"; run_i; run_u --purge; ok "old --purge flag still accepted" $?

fresh "$LUA_STOCK" "$MENU_ENTRIES"; run_i; b1=$(cat "$H/hyprland.lua"); run_u --dry-run
[[ "$(cat "$H/hyprland.lua")" == "$b1" ]]; ok "uninstall --dry-run changes nothing" $?

"$ROOT/install" --bogus >/dev/null 2>&1; [[ $? != 0 ]]; ok "unknown option rejected" $?

# ── folder deletion safety (never deletes anything here; just asks) ──────────
G="$TMP/git"; mkdir -p "$G"
can() { "$ROOT/bin/setup.sh" _can-delete "$1" >/dev/null 2>&1; }
gitq() { git -C "$1" -c user.name=t -c user.email=t@t "${@:2}" >/dev/null 2>&1; }
git init -q --bare "$G/remote.git"
git clone -q "$G/remote.git" "$G/pushed" 2>/dev/null; echo a > "$G/pushed/f"; gitq "$G/pushed" add f; gitq "$G/pushed" commit -m a; gitq "$G/pushed" push origin HEAD
can "$G/pushed"; ok "clean + pushed checkout may be deleted" $?
echo b >> "$G/pushed/f"; ! can "$G/pushed"; ok "uncommitted changes protect the folder" $?
gitq "$G/pushed" checkout f; echo new > "$G/pushed/untracked"; ! can "$G/pushed"; ok "untracked files protect the folder" $?
rm "$G/pushed/untracked"; echo c >> "$G/pushed/f"; gitq "$G/pushed" commit -am c; ! can "$G/pushed"; ok "unpushed commits protect the folder" $?
gitq "$G/pushed" push origin HEAD; echo d >> "$G/pushed/f"; gitq "$G/pushed" stash; ! can "$G/pushed"; ok "stashes protect the folder" $?
git init -q "$G/local"; echo a > "$G/local/f"; gitq "$G/local" add f; gitq "$G/local" commit -m a
! can "$G/local"; ok "no remote (only copy) protects the folder" $?
mkdir -p "$G/plain"; ! can "$G/plain"; ok "non-git folder is protected" $?
! can "$ROOT"; ok "this dev checkout (unpushed) is protected" $?

echo
if (( failed )); then echo "$failed failed"; exit 1; fi
echo "all passed"
