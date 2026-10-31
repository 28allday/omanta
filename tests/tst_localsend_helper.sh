#!/bin/sh
set -eu
helper=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
export OMANTA_LOCALSEND_TEST_DIR="$scratch"
cat > "$scratch/localsend" <<'MOCK'
#!/bin/sh
jq -cn --args '$ARGS.positional' -- "$@" > "$OMANTA_LOCALSEND_TEST_DIR/desktop"
MOCK
cat > "$scratch/omarchy-shell" <<'MOCK'
#!/bin/sh
[ "$1" = bredda.localsend ] && [ "$2" = selectPaths ] || exit 1
printf '%s' "$3" > "$OMANTA_LOCALSEND_TEST_DIR/bar"
printf '%s\n' "${OMANTA_LOCALSEND_TEST_REPLY:-ok}"
exit "${OMANTA_LOCALSEND_TEST_STATUS:-0}"
MOCK
cat > "$scratch/notify-send" <<'MOCK'
#!/bin/sh
printf '%s\n' "$@" > "$OMANTA_LOCALSEND_TEST_DIR/error"
MOCK
chmod +x "$scratch/localsend" "$scratch/omarchy-shell" "$scratch/notify-send"
export PATH="$scratch:$PATH"
file="$scratch/space and \"quote\".txt"
folder="$scratch/folder"
newline="$scratch/line
break.txt"
touch "$file" "$newline"
mkdir "$folder"
jq -cn --args '$ARGS.positional' -- "$file" "$folder" "$newline" > "$scratch/expected"

# Default and explicit desktop pass exact paths, without headless flags.
unset OMANTA_OPTION_BACKEND
sh "$helper" "$file" "$folder" "$newline"
cmp "$scratch/desktop" "$scratch/expected"
[ ! -e "$scratch/bar" ]
OMANTA_OPTION_BACKEND=desktop sh "$helper" "$file" "$folder" "$newline"
cmp "$scratch/desktop" "$scratch/expected"
rm "$scratch/desktop"

OMANTA_OPTION_BACKEND=bar sh "$helper" "$file" "$folder" "$newline"
jq -c '.paths' "$scratch/bar" > "$scratch/actual"
cmp "$scratch/actual" "$scratch/expected"
[ ! -e "$scratch/desktop" ]
rm "$scratch/bar"

# Missing support, shell errors, bad settings and bad paths must not fall
# back to the desktop app and accidentally start a competing receiver.
export OMANTA_OPTION_BACKEND=bar
for reply in 'Function not found.' 'LocalSend receiver is not ready'; do
    if OMANTA_LOCALSEND_TEST_REPLY="$reply" sh "$helper" "$file" 2>/dev/null; then exit 1; fi
    [ -s "$scratch/error" ]
done
if OMANTA_LOCALSEND_TEST_STATUS=1 sh "$helper" "$file" 2>/dev/null; then exit 1; fi
rm "$scratch/bar"
if OMANTA_OPTION_BACKEND=unknown sh "$helper" "$file" 2>/dev/null; then exit 1; fi
if sh "$helper" "$scratch/missing" 2>/dev/null; then exit 1; fi
if sh "$helper" relative 2>/dev/null; then exit 1; fi
if sh "$helper" 2>/dev/null; then exit 1; fi
[ ! -e "$scratch/desktop" ]
[ ! -e "$scratch/bar" ]
printf 'PASS: LocalSend backend routing and failures\n'
