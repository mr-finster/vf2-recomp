#!/usr/bin/env python3
# midicmp.py DIR [WINDOW]: what each player's sound board was sent in a vf2net run (VF2NET_MIDI=DIR) against the
# straight run's (the same inputs, no rollback), as whole MIDI messages (each with its status byte). Effects are note-ons
# (0x9n, velocity over 0: the game's sounds and voices, one-shots); every other message (control changes, the music's
# cues and stops, all notes off, aftertouch) is the music's or the sound program's state.
# - The state's messages: the order the side sent them in against the straight run's (difflib): the same sequence or
#   not, those missing and extra; each matched one's lag, the live frame it went at less the straight run's frame.
# - Each effect of the straight run matched with one the side sent: the same message, sent within WINDOW frames
#   (default 8) of the straight run's frame, the nearest first: on time, late or early by so many frames; dropped
#   (none sent); extra (sent, the straight run never made it: a wrong guess's).
import difflib, struct, sys
from collections import defaultdict

def data_bytes(st):
    if st >= 0xf0: return {0xf1: 1, 0xf2: 2, 0xf3: 1}.get(st, 0)
    return 1 if (st & 0xf0) in (0xc0, 0xd0) else 2

def read(path):
    d = open(path, 'rb').read()
    i, out, status, part = 0, [], 0, []
    while i < len(d):
        live, made, n = struct.unpack_from('<III', d, i)
        i += 12
        for x in d[i:i + n]:
            if x >= 0xf8: out.append((live, (x,))); continue
            if x & 0x80:
                part = [x]; status = 0 if x >= 0xf0 else x
                if data_bytes(x) == 0: out.append((live, tuple(part))); part = []
                continue
            if not part:
                if not status: out.append((live, (x,))); continue
                part = [status]
            part.append(x)
            if len(part) == 1 + data_bytes(part[0]): out.append((live, tuple(part))); part = []
        i += n
    return out

def is_effect(m):
    return len(m) == 3 and (m[0] & 0xf0) == 0x90 and m[2] > 0

def lags(d):
    return ', '.join('%d: %d' % (a, b) for a, b in sorted(d.items())) or '-'

def state(straight, sent):
    a = [x for x in straight if not is_effect(x[1])]
    b = [x for x in sent if not is_effect(x[1])]
    sm = difflib.SequenceMatcher(None, [c for _, c in a], [c for _, c in b], autojunk=False)
    lag = defaultdict(int)
    same = 0
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            lag[b[j + k][0] - a[i + k][0]] += 1
            same += 1
    return len(a), same, len(a) - same, len(b) - same, lag

def effects(straight, sent, window):
    by = defaultdict(list)
    for i, (live, c) in enumerate(sent):
        if is_effect(c): by[c].append(i)
    used = set()
    n = dropped = 0
    lag = defaultdict(int)
    for f, c in straight:
        if not is_effect(c): continue
        n += 1
        best = None
        for i in by[c]:
            if i in used or abs(sent[i][0] - f) > window: continue
            key = (abs(sent[i][0] - f), i)
            if best is None or key < best[0]: best = (key, i)
        if best is None:
            dropped += 1
            continue
        used.add(best[1])
        lag[sent[best[1]][0] - f] += 1
    extra = sum(len(v) for v in by.values()) - len(used)
    return n, lag, dropped, extra

def main():
    d = sys.argv[1]
    window = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    straight = read(d + '/straight.mid')
    last = max((f for f, _ in straight), default=0)
    for side in ('host', 'guest'):
        sent = [x for x in read(d + '/' + side + '.mid') if x[0] <= last + 10]
        n, lag, dropped, extra = effects(straight, sent, window)
        on = lag.get(0, 0)
        late = sum(v for a, v in lag.items() if a > 0)
        early = sum(v for a, v in lag.items() if a < 0)
        print('%-5s effects: straight %4d: on time %4d, late %3d, early %2d (frames: %s); dropped %2d, extra %2d' % (side, n, on, late, early, lags(lag), dropped, extra))
        n, same, missing, more, lag = state(straight, sent)
        print('%-5s music:   straight %4d: %s; matched %4d (lag frames: %s), missing %d, extra %d' % (
            side, n, 'the same sequence' if missing == 0 and more == 0 else 'NOT the same sequence', same, lags(lag), missing, more))

main()
