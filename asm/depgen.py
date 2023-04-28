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
        path = pathlib.Path(filename).parent
        with open(filename) as input_file:
            for line in input_file:
                match = re.search(r"\s+include ([\w/.]+)", line)
                if match:
                    include_file = pathlib.Path(match.group(1))
                    if include_file.suffix == '':
                        include_file = include_file.with_suffix('.inc')
                    include_path = path.joinpath(include_file).as_posix()
                    if include_path not in includes:
                        includes.add(include_path)
                        to_parse.append(include_path)

    print(f'{p_file}: {" ".join(includes)}')
