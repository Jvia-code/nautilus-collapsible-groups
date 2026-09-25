#!/usr/bin/env python3
"""Regression check for Nautilus MIME string ownership in grouped view."""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'src-work' / 'nautilus-50.3'
model = (source / 'src/nautilus-view-model.c').read_text()
header = (source / 'src/nautilus-file.h').read_text()
test_source = (Path(__file__).resolve().parent / 'test-groups.c').read_text()
run_sh = (Path(__file__).resolve().parent / 'run.sh').read_text()

assert re.search(r'const\s+char\s*\*\s*nautilus_file_get_mime_type\s*\(', header), 'Nautilus MIME API must be borrowed const char *'
assert 'const char *mime = nautilus_file_get_mime_type (file);' in model
assert 'return mime != NULL ? g_strdup (mime) : g_strdup ("application/octet-stream");' in model
assert 'const char *nautilus_file_get_mime_type (NautilusFile *file)' in test_source
assert 'g_autofree char *mime=nautilus_file_get_mime_type' not in test_source
assert 'const char *nautilus_file_get_mime_type(NautilusFile *file);' in run_sh
print('OK: borrowed MIME strings are duplicated before g_autofree ownership.')
