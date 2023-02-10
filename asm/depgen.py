#!/usr/bin/python3

import pathlib
import re
import sys

if __name__ == '__main__':
    if len(sys.argv) != 2:
        exit(-1)

    p_file = pathlib.Path(sys.argv[1]).stem + '.p'
    includes = set()
    to_parse = [sys.argv[1]]
    while len(to_parse) > 0:
        filename = to_parse.pop()
        with open(filename) as input_file:
            for line in input_file:
                match = re.search(r"\s+include (\w+)", line)
                if match:
                    include = match.group(1) + '.inc'
                    if include not in includes:
                        includes.add(include)
                        to_parse.append(include)

    print(f'{p_file}: {" ".join(includes)}')
