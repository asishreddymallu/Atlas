"""Reads one line of the sweep probe's `run` output.

  <id> OK pc=<hex> steps=<n> cycles=<n> x0..x31 f0..f31 fcsr mem0..mem31
  <id> ERR <message>

Fields of the form key=value come first and may vary (older probes had no
cycles=); everything after them is hex. Returns (id, None, message) for an
ERR line, else (id, {key: value}, [ints]).
"""


def parse_line(line):
    p = line.split()
    if p[1] != "OK":
        return p[0], None, " ".join(p[2:])
    kv, i = {}, 2
    while i < len(p) and "=" in p[i]:
        k, v = p[i].split("=", 1)
        kv[k] = v
        i += 1
    return p[0], kv, [int(x, 16) for x in p[i:]]
