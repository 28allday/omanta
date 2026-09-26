#!/bin/sh
set -eu
helper=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
export OMANTA_HELPER_TEST_DIR="$scratch"
cat > "$scratch/socat" <<'MOCK'
#!/bin/sh
cat > "$OMANTA_HELPER_TEST_DIR/request"
printf 'link\thttps://example.invalid/share\n'
MOCK
cat > "$scratch/wl-copy" <<'MOCK'
#!/bin/sh
cat > "$OMANTA_HELPER_TEST_DIR/clipboard"
MOCK
cat > "$scratch/xdg-open" <<'MOCK'
#!/bin/sh
printf %s "$1" > "$OMANTA_HELPER_TEST_DIR/opened"
MOCK
chmod +x "$scratch/socat" "$scratch/wl-copy" "$scratch/xdg-open"
export PATH="$scratch:$PATH"
sh "$helper" copy '/tmp/ordinary file.txt'
[ "$(cat "$scratch/clipboard")" = 'https://example.invalid/share' ]
printf 'get_shared_link\npath\t/tmp/ordinary file.txt\ndone\n' > "$scratch/expected"
cmp "$scratch/request" "$scratch/expected"
sh "$helper" open '/tmp/ordinary file.txt'
[ "$(cat "$scratch/opened")" = 'https://example.invalid/share' ]
rm "$scratch/request"
status=0
sh "$helper" copy '/tmp/bad
name' 2>/dev/null || status=$?
[ "$status" -eq 2 ]
[ ! -e "$scratch/request" ]
