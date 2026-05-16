#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 <kernel-elf>" >&2
	exit 1
fi

elf="$1"

cat <<'EOF'
#include <stddef.h>
#include <stdint.h>
#include <debug/kallsyms.h>

const struct kallsyms_entry kallsyms[] = {
EOF

nm -n -S --defined-only "$elf" | awk '
function esc(s,    out) {
	gsub(/\\/,"\\\\",s)
	gsub(/"/,"\\\"",s)
	return s
}

$3 ~ /^[TtWw]$/ {
	printf("	{ 0x%sULL, 0x%sULL, \"%s\" },\n", $1, $2, esc($4))
}
'

cat <<'EOF'
};

const size_t kallsyms_count = sizeof(kallsyms) / sizeof(kallsyms[0]);
EOF
