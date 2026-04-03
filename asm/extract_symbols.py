#!/usr/bin/env python3
"""Extract symbols from ASL listing files and write a simple address->name map.

Output format (one symbol per line):
    ADDR name
where ADDR is a 4-digit uppercase hex address and name is the full symbol
name (e.g. "validate_name.loop"). Dotted names represent local labels inside
the enclosing non-dotted label.
"""
import re
import sys
import argparse

_SYMBOL_TABLE_HEADER = "Symbol Table (* = unused):"
# Matches one symbol entry within a '|'-delimited segment.
# Leading '*' marks unused symbols (included anyway).
# Name may have a [section] suffix that is stripped.
# Address is 1-4 hex digits; type is 'C' (code) or '-' (data/other).
_SYMBOL_RE = re.compile(r'\*?(\S+?)\s*:\s+([0-9A-Fa-f]{1,4})\s+[C\-]')


def extract_symbols(listing_path):
    """Return a dict of {address: name} from an ASL listing file."""
    with open(listing_path) as f:
        content = f.read()

    pos = content.find(_SYMBOL_TABLE_HEADER)
    if pos == -1:
        print(f"warning: no symbol table in {listing_path}", file=sys.stderr)
        return {}

    pos = content.find('\n', pos)
    if pos == -1:
        return {}

    symbols = {}
    for line in content[pos + 1:].splitlines():
        for segment in line.split('|'):
            m = _SYMBOL_RE.search(segment)
            if not m:
                continue
            name, addr_str = m.group(1), m.group(2)
            # Strip optional [section] suffix.
            bracket = name.find('[')
            if bracket != -1:
                name = name[:bracket]
            if not name:
                continue
            addr = int(addr_str, 16)
            if addr > 0xFFFF:
                continue
            symbols[addr] = name
    return symbols


def main():
    parser = argparse.ArgumentParser(
        description="Extract symbols from ASL listing files into a simple map")
    parser.add_argument('listings', nargs='+', metavar='LISTING',
                        help='ASL .lst files (later files override earlier ones)')
    parser.add_argument('-o', '--output', default='-',
                        metavar='FILE', help='Output file (default: stdout)')
    args = parser.parse_args()

    all_symbols = {}
    for path in args.listings:
        all_symbols.update(extract_symbols(path))

    lines = [f"{addr:04X} {name}\n"
             for addr, name in sorted(all_symbols.items())]

    if args.output == '-':
        sys.stdout.writelines(lines)
    else:
        with open(args.output, 'w') as f:
            f.writelines(lines)


if __name__ == '__main__':
    main()
