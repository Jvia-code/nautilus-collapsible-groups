#!/usr/bin/env bash
set -euo pipefail
source_dir=${1:?Indiquer le dossier des sources Nautilus modifiees}
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_build=$(mktemp -d)
trap 'rm -rf -- "$test_build"' EXIT
cp -- "$source_dir/src/nautilus-view-model.c" "$source_dir/src/nautilus-view-model.h" "$source_dir/src/nautilus-view-item.c" "$source_dir/src/nautilus-view-item.h" "$test_dir/test-groups.c" "$test_build/"
cat > "$test_build/nautilus-types.h" <<'HEADER'
#pragma once
#include <glib-object.h>
typedef struct _NautilusFile NautilusFile;
typedef struct _NautilusDirectory NautilusDirectory;
typedef struct _NautilusViewItem NautilusViewItem;
HEADER
cat > "$test_build/nautilus-file.h" <<'HEADER'
#pragma once
#include "nautilus-types.h"
#define NAUTILUS_TYPE_FILE (nautilus_file_get_type())
G_DECLARE_FINAL_TYPE(NautilusFile,nautilus_file,NAUTILUS,FILE,GObject)
struct _NautilusFile { GObject parent; };
NautilusFile *nautilus_file_get_parent(NautilusFile *file);
gboolean nautilus_file_is_directory(NautilusFile *file);
const char *nautilus_file_get_mime_type(NautilusFile *file);
HEADER
cat > "$test_build/nautilus-directory.h" <<'HEADER'
#pragma once
#include "nautilus-file.h"
NautilusFile *nautilus_directory_get_corresponding_file(NautilusDirectory *directory);
HEADER
: > "$test_build/nautilus-global-preferences.h"
test_main="$test_build/test-groups.c"
if [[ ${2:-} == --ui ]]; then
    cp -- "$test_dir/test-ui.c" "$test_build/"
    python3 - "$source_dir/src/nautilus-list-view.c" "$test_build/group-cells.inc.c" <<'EXTRACT'
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
start=s.index('static GtkWidget *\nfile_cell (')
end=s.index('static const NautilusViewInfo list_view_info =',start)
Path(sys.argv[2]).write_text(s[start:end])
EXTRACT
    test_main="$test_build/test-ui.c"
fi
read -r -a gtk_cflags <<< "$(pkg-config --cflags gtk4)"
read -r -a gtk_libs <<< "$(pkg-config --libs gtk4)"
cc -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration \
   -Werror=incompatible-pointer-types -g "${gtk_cflags[@]}" \
   "$test_build/nautilus-view-model.c" "$test_build/nautilus-view-item.c" "$test_main" \
   "${gtk_libs[@]}" -o "$test_build/test-groups"
G_DEBUG=fatal-warnings "$test_build/test-groups"
