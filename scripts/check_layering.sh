#!/usr/bin/env bash
# Layering guard for island.
#
# src/core is platform-agnostic: it must not call GL/GLFW/glad or any
# PSP/KOS/Dreamcast symbol. Everything platform-specific lives in
# src/backend/<platform>. Comments are stripped first, so prose that merely
# mentions the API (e.g. "never GL/GLFW") does not trip the check.
#
# cglm/glm_ math is intentionally allowed in core.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
core="$root/src/core"

# GL calls/enums (glClear, GL_*), the loaders/window libs, and console SDK
# symbol families. Matched against comment-stripped source only.
pattern='GL_|gl[A-Z][A-Za-z0-9_]*|glfw|GLFW|glad|GLAD|PSP|pspgu|sceGu|KOS|DreamOS|Dreamcast|pvr|PVR|sgl|SGL'

failed=0
while IFS= read -r file; do
    # Blank out block-comment contents (keeps newlines, so line numbers hold),
    # then drop line comments, then search what remains.
    hits="$(perl -0777 -pe 's{/\*.*?\*/}{ (my $c=$&) =~ s/[^\n]//g; $c }gse; s{//[^\n]*}{}g' "$file" \
        | grep -nE "$pattern" || true)"
    if [ -n "$hits" ]; then
        echo "Layering violation in $file:" >&2
        echo "$hits" >&2
        failed=1
    fi
done < <(find "$core" -type f \( -name '*.c' -o -name '*.h' \))

if [ "$failed" -ne 0 ]; then
    echo "FAIL: src/core must stay platform-agnostic (see island1-jimd)" >&2
    exit 1
fi

echo "layering check passed: src/core is platform-agnostic"
