"""Decode printer data and compare pixel-for-pixel with the raw raster dump."""
import re, sys

def decode(data):
    pages, i = [], 0
    while (s := data.find(b"\x1b*b1030m", i)) >= 0:
        i, lines, prev = s + 8, [], {}
        while not data.startswith(b"1030M\x0c", i):
            m = re.compile(rb"(\d+)w").match(data, i); assert m, data[i:i+20]
            p = m.end(); end = p + int(m[1]); assert data[p] == 0
            n = data[p+1]; p += 2; prev = {}          # no state may cross blocks
            for _ in range(n):
                cnt = data[p]; p += 1
                if cnt == 255: cur = None
                else:
                    cur, x = dict(prev) if prev is not None else None, 0
                    blank = cur is None
                    cur = cur if cur is not None else {}
                    for _ in range(cnt):
                        c = data[p]; p += 1
                        rep = c & 128
                        off, om, k, km = ((c>>5)&3, 3, c&31, 31) if rep else ((c>>3)&15, 15, c&7, 7)
                        for which in (0, 1):
                            v, mx = (off, om) if which == 0 else (k, km)
                            if v == mx:
                                while True:
                                    e = data[p]; p += 1; v += e
                                    if e != 255: break
                            if which == 0: off = v
                            else: k = v
                        x += off
                        ln = k + 2 if rep else k + 1
                        for j in range(ln): cur[x+j] = data[p] if rep else data[p+j]
                        p += 1 if rep else ln; x += ln
                    if blank: cur["blankbase"] = True
                lines.append(cur); prev = cur
            assert p == end, (p, end); i = end
        pages.append(lines)
    return pages

def main(raw, out):
    raw, pages = open(raw, "rb").read(), decode(open(out, "rb").read())
    i = n = 0
    while i < len(raw):
        e = raw.index(b"\n", i); h, w = map(int, raw[i:e].split()); i = e + 1
        assert len(pages[n]) == h, (len(pages[n]), h)
        base = {}
        for y in range(h):
            want, got = raw[i:i+w], pages[n][y]; i += w
            if got is None: row = bytes(w)
            else:
                assert got.get("blankbase") or all(x in got for x in range(w)), f"page {n} line {y}: depends on unknown data"
                assert all(x == "blankbase" or x < w for x in got)
                row = bytes(got.get(x, 0) for x in range(w))
            assert row == want, f"page {n} line {y} differs"
        n += 1
    assert n == len(pages)
    print(f"OK: {n} page(s) identical")
main(*sys.argv[1:])
