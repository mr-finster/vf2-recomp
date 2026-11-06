#!/usr/bin/env python3
# The glossary (docs/i18n-glossary.md): the menus' main terms in every language side by side, read from the tables
# (src/host/lang/*.inc), so it can't drift from them. A translator keeps a term the same wherever it's used.
#   python3 tools/glossary.py > docs/i18n-glossary.md
import os
import re

here = os.path.dirname(os.path.abspath(__file__))
lang_dir = os.path.join(here, '..', 'src', 'host', 'lang')
CODES = ['en', 'ko', 'ja', 'zh-Hans', 'zh-Hant', 'es', 'pt-BR', 'fr', 'ru', 'de', 'it', 'pl', 'tr']
# (the term's ID, and a word on what it is)
TERMS = [
    ('P_ONLINE', 'online play, the Online page'),
    ('QUICK_MATCH_TAG', 'matchmaking with a stranger near you (the Random of older versions)'),
    ('P_ROOM', 'a room: a host waiting, players in line, watchers'),
    ('P_CREATE_ROOM', 'opening a room'),
    ('LISTED_IN_ROOMS', 'a room shown in the public list'),
    ('P_ENTER_CODE', "typing a room's code"),
    ('COL_HOST', "the room's owner, player 1"),
    ('ROW_JOIN', 'to play in a room'),
    ('ROW_WATCH', 'to spectate'),
    ('COL_WATCHING', 'spectators (a count)'),
    ('PLAY_NEXT', 'to wait in a room for your turn'),
    ('BUTTON_PLAY_AGAIN', 'a rematch'),
    ('BUTTON_NEW_OPPONENT', 'back to matchmaking'),
    ('MR_LEAVE', 'ending a match early'),
    ('OS_DELAY', 'input delay (frames)'),
    ('CELL_RB', 'rollback'),
    ('VS_RELAY', 'the relay server (not a direct connection)'),
    ('VS_DIRECT', 'a direct connection'),
    ('OS_HIDE_IP', 'a setting'),
    ('P_SETTINGS', 'settings'),
    ('P_DISPLAY', 'the Display page'),
    ('D_SCALE', 'render scale'),
    ('D_AA', 'antialiasing'),
    ('D_RENDERER', 'the 3D renderer (GPU, CPU)'),
    ('D_FULLSCREEN', 'fullscreen'),
    ('D_SCREEN_FILTER', 'scanlines, CRT'),
    ('P_SOUND', 'the Sound page'),
    ('P_GAME', 'the Game page'),
    ('G_LANGUAGE', 'the language setting'),
    ('G_PLAY_STYLE', 'PC (free play) or Arcade (coins)'),
    ('G_ARCADE', 'the arcade way'),
    ('M_ARCADE', "the arcade's own settings"),
    ('A_DIFFICULTY', 'the CPU\'s difficulty'),
    ('A_ROUNDS_VS', 'rounds to win a match'),
    ('A_RING', "the ring's size (the arcade's STAGE WIDTH)"),
    ('A_CONTINUE', 'continuing after losing to the CPU'),
    ('A_VERSION', "the arcade's own: the original or Version 2.1"),
    ('P_CHEATS', 'cheats'),
    ('FIGHTER_NORMAL', "a player's fighter: picked at the select as ever"),
    ('FIGHTER_RANDOM', 'a random fighter: three of the cast made one'),
    ('FIGHTER_SEED', "a random fighter's name, which makes the same one anywhere"),
    ('C_RANDOM_CPU', 'every computer opponent a random fighter'),
    ('C_SECRET', "Dural, MUE or KOJAC in Akira's spot"),
    ('F_YOUR_FIGHTER', "Online > Settings: this player's fighter online"),
    ('P_CONTROLS', 'controls'),
    ('CTL_PAD', 'a gamepad'),
    ('CTL_GUARD', 'a button: Guard'),
    ('CTL_PUNCH', 'a button: Punch'),
    ('CTL_KICK', 'a button: Kick'),
    ('CTL_COIN', 'a credit'),
    ('CTL_SETUP', 'mapping a pad\'s buttons'),
    ('TITLE_PAUSED', "the menu's title"),
    ('TITLE_ONLINE_MATCH', "the menu's title in a match"),
    ('WATCHING', 'spectating (the title)'),
    ('MATCH_OVER', 'the end of a match'),
    ('CONNECTION_LOST', 'a dropped connection'),
    ('FINDING_OPPONENT', 'matchmaking'),
    ('M_RESUME', 'back to the game'),
    ('BACK', 'back a page'),
    ('M_QUIT', 'closing the game'),
    ('ON', 'a setting on'),
    ('OFF', 'a setting off'),
    ('AUTO', 'automatic'),
]


def unquote(lit):
    out, i, b = [], 0, lit.encode('utf-8')
    while i < len(b):
        if b[i:i + 2] == b'\\x':
            out.append(int(b[i + 2:i + 4], 16))
            i += 4
        elif b[i] == 0x5C:
            out.append(b[i + 1])
            i += 2
        else:
            out.append(b[i])
            i += 1
    return bytes(out).decode('utf-8')


def table(code):
    t = {}
    for line in open(os.path.join(lang_dir, code + '.inc'), encoding='utf-8'):
        m = re.match(r'T\((\w+), "((?:[^"\\]|\\.)*)"\)', line)
        if m:
            t[m.group(1)] = unquote(m.group(2)).replace(' ', ' ').replace('|', '\\|')
    return t


tables = {c: table(c) for c in CODES}
print('# Glossary: the menus\' terms in every language\n')
print('Made by `tools/glossary.py` from the tables (`src/host/lang/*.inc`): change a term there, then run it again. A term is')
print('the same wherever it appears (a page\'s name, a button, a help line). The game\'s own name (Virtua Fighter 2), the')
print('fighters\' (Dural, MUE, KOJAC), a seed\'s, key and button names as printed (Start, Enter, A), "VS" and the ROM\'s own text')
print('are never translated; a room without a name keeps "NAME\'S ROOM" in English everywhere (everyone sees the same name).\n')
print('| ID | what it is | ' + ' | '.join(CODES) + ' |')
print('|' + '---|' * (len(CODES) + 2))
for k, what in TERMS:
    print('| `%s` | %s | %s |' % (k, what, ' | '.join(tables[c].get(k, '') for c in CODES)))
