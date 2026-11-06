# The menus in other languages (i18n)

Virtua Fighter 2 Recomp's menus, notices, online screens and its own system dialogs (choosing vf2.zip the first time,
a start that failed) are in 13 languages: English, Korean, Japanese, Simplified and Traditional Chinese, Spanish,
Brazilian Portuguese, French, Russian, German, Italian, Polish and Turkish. A pseudo-locale is there for testing. It is Virtua Fighter 3 Recomp's system, shared by the three games: the
engine (`src/host/i18n.*`, `text.*`), the RmlUi changes, the fonts' script and the tests are the same files in each;
each game has its own tables (`src/host/lang/*.inc`), its own fonts (cut to its own strings) and its own pages. This
page covers:

- how it works;
- how to add a string or a language;
- the fonts;
- the tests;
- the glossary and what still needs a native speaker;
- what is Virtua Fighter 2's own, and what is shared.

What is not translated: the game's own text (the ROM's: its title screen, HUD and so on), players' and rooms' names,
codes, addresses, key and button names as printed (Enter, Esc, A, LB, Start), the game's name, the fighters' names in
the values (Dural, MUE, KOJAC), a random fighter's seed (its name) and "VS".

The language never touches the game or online play:
- it is not sent in the protocol and not part of a save or the game's state;
- the log stays in English;
- the HUD's names stay as they are ("PLAYER 1");
- a room without a name is "NAME'S ROOM" in English everywhere, because everyone sees the same name.

## The mechanism (src/host/i18n.h, i18n.cpp, text.h, text.cpp, lang/*.inc)

- **One table per language, whole sentences.** `src/host/lang/en.inc` is the list of strings, `T(ID, "English")`, each
  with a comment saying where it is shown and how much room it has. Every other language has its own file
  (`ko.inc`, `ja.inc`, `zh-Hans.inc`, ...) with the same IDs in any order. A string is a whole sentence. What
  changes in it is a named place, `{name}`, `{code}`, `{n}`, and a translation puts the place where its language
  needs it. There is no concatenation of pieces: the word order is the translation's.
- **IDs.** `text.h` turns each line of `en.inc` (an X-macro) into a constant: `tr::ONLINE`, `tr::D_SCALE_HELP`. A
  `tr::text` converts to `std::string` in the language currently on (`operator std::string`, `c_str()`), so it goes
  anywhere a string did. A missing translation falls back to English, which `langtest` catches.
- **Filling places.** `tr::fill(tr::IN_LINE, {{"code", c}, {"n", "2"}, {"of", "3"}})` fills the places in one pass;
  what is filled in is never filled again.
- **Messages kept for later.** A notice, a failure's why and an error's line are a `tr::line`: an ID with its places,
  or a raw string. `.str()` renders it in the language on at the time it is drawn, so a language switch re-renders
  it. `.en()` renders it in English for the log.
- **src/net's sentences.** `tr::from_net(english)` maps the shared net code's English sentences (version refusals, a
  desync at a frame, socket errors) to `NET_*` IDs, their places pulled out, so src/net stays the same in every game
  and the menus still show them translated. An unknown one is shown as it came.
- **The setting.** Game > Language: Auto, then each language in its own name and script ("한국어", "Русский").
  Auto follows the system (`SDL_GetPreferredLocales`). The system's first language that the game has wins:
  - zh-TW, zh-HK and zh-MO (or a script tag Hant) give Traditional Chinese; any other Chinese gives Simplified;
  - any Portuguese gives pt-BR;
  - anything else that isn't covered gives English.

  `settings.ini` stores it as `language = auto|en|ko|ja|zh-Hans|zh-Hant|es|pt-BR|fr|ru|de|it|pl|tr`, and the language
  is set as soon as settings.ini is read, so the ROM's first-run boxes and a failed start's come up in it (`ROMS_*`,
  `FAIL_*`; a set's or the system's own words in them stay English, as does the log). Left or right on
  the row switches the language at once (`set_language` in main.cpp): `tr::set`, then `game_menu::set_language()`,
  which re-orders the fallback fonts, sets the documents' class (`lang-<code>`, plus `cjk`), re-renders the key hints
  and rebuilds the page. main.cpp's `menus_relabelled` then remakes the title, the main page and Game, with Language
  still selected.
- **For tests.** `VF_LANG=<code>` (or `pseudo`) overrides the setting for a run.
- **Pseudo-locale.** `VF_LANG=pseudo` gives accented letters, about 40% longer, in brackets: "[Ŕéñđéŕ šçáĺé ~~~]".
  It finds text that isn't going through the tables (it stays plain) and the boxes that are too tight.

### How to add a string

1. Add `T(MY_ID, "The English sentence with a {place}.")   // where it shows, how much room` to `en.inc`, in its
   section.
2. Use `tr::MY_ID` in the code, or `tr::fill(tr::MY_ID, {{"place", value}})`. For a message drawn later, use a
   `tr::line(tr::MY_ID, {{...}})`.
3. Add the line to every other `lang/*.inc`. If there is no translation yet, add the English: it is listed, not
   hidden. Keep the glossary's terms (`docs/i18n-glossary.md`). For CJK, use full-width punctuation, but keep ASCII
   for codes and numbers.
4. Build and run `langtest`. It fails on:
   - an ID missing, doubled or extra in any language;
   - a translation whose `{places}` differ from the English;
   - a letter that is in no font for that language (re-run `tools/fonts/subset.py`, see Fonts);
   - a string that starts or ends with a space.
5. Run `tools/langcheck.py`. It runs menutest in every language and lists every box the text overflows where the
   English doesn't, and every check the English passes that this language fails. Shorten the translation, or give
   the layout room without changing the English picture.

There are no plural rules. Where a count changes the word, the strings are written so that they don't need one. The
Polish and Russian ones say "pokoje: {n}" and "зрителей: {n}"; Korean, Japanese and Chinese have no plural. A
language that needs real plurals would add an ID per form, chosen in the code.

### How to add a language

1. Add it to the `tr::lang` enum (before `pseudo`), to `LANGS` and `REAL_LANGS`, to `info()` (its code, its name in
   its own script, its English name, CJK or not), and to `from_locale` if a system locale needs mapping.
2. Add `lang/<code>.inc` and add it to `text.cpp` (an `#include` and the `TABLE` and `TABLE_COUNT` entries; a
   `static_assert` counts them).
3. Fonts: if Barlow Condensed doesn't have its letters, add a subset (`tools/fonts/subset.py`) and put the face in
   `FALLBACK_FONTS` (menu.cpp) and `fallback_order()`.
4. Add it to `tools/langcheck.py`'s list, and to the Online site page's `LANGS` and `TR` (shared by the three games, in
   Virtua Fighter 3 Recomp's `tools/online/site/online.html`; `node tools/online/sitelang.mjs` there).
5. Run `langtest`, `langcheck`, and look at the pictures.
## Fonts and their size

Barlow Condensed stays the face for every Latin letter and number in every language. Its Latin Extended covers
Spanish, Portuguese, French, German, Italian, Polish and Turkish (langtest checks every letter). Everything else comes
from Noto Sans subsets, used as RmlUi fallback faces, cut to Virtua Fighter 2's own strings:

| script | files | bytes | letters |
|---|---|---|---|
| Korean | NotoSansKR-Medium, -Bold | 143,448 | 453 (every Hangul syllable the strings use) |
| Japanese | NotoSansJP-Medium, -Bold | 346,980 | 524 (the kanji used, plus all the kana) |
| Simplified Chinese | NotoSansSC-Medium, -Bold | 319,576 | 608 |
| Traditional Chinese | NotoSansTC-Medium, -Bold | 370,472 | 609 |
| Cyrillic | NotoSansCyr-Medium, -SemiBold, -BoldItalic (condensed, wdth 75) | 31,196 | 66 |
| **total** | 11 files | **1,211,672 (1.16 MB)** | |

Each language's fonts are added to the download, not loaded unless that language is on. The licences
(`assets/fonts/OFL-NotoSansCJK.txt`, `OFL-NotoSans.txt`) and the credit (`assets/fonts/README.txt`, README.md) go
with them, and `tools/release/licenses.sh` includes them. To remake the fonts after a translation adds a letter:

```
cd vf2-recomp
pip install fonttools
python3 tools/fonts/subset.py <folder with NotoSansKR[wght].ttf, NotoSansJP[wght].ttf, NotoSansSC[wght].ttf,
                               NotoSansTC[wght].ttf, NotoSans[wdth,wght].ttf, NotoSans-Italic[wdth,wght].ttf>
```

(These are the variable fonts from google/fonts' `ofl/notosans*`.) The script reads every `lang/*.inc`, keeps only
the letters Barlow lacks, instances the weights and writes `assets/fonts/NotoSans*.ttf`.

**RmlUi changes** (third_party/rmlui, all small, the same diff as Virtua Fighter 3 Recomp's):

- `FontProvider` orders fallback faces by family priority, then style match, then nearest weight
  (`Rml::SetFallbackFontOrder`). The current language's script comes first, so a character shared by Chinese and
  Japanese is drawn in that language's own form.
- Faces remember their style and weight (`FontFaceHandleDefault`).
- `ElementText` breaks lines between CJK characters, with the usual kinsoku rules: no break before 、。）」ー or small
  kana, and no break after （「. Without this, RmlUi breaks only at spaces, and Japanese and Chinese ran off the
  right edge.

CJK is never slanted: there is no synthetic italic, and the title's italic falls back to the upright face. English is
unchanged: with `language = en`, menutest's pictures from before this change (53, or 63 with a lobby) are
pixel-identical.

**Layout.**

- In languages other than English, a page's help note (`#note`) shrinks from 17dp down to 14dp until it fits.
- Translations were shortened wherever `langcheck` found a box that the English fits.
- English's own long lines were left alone, so that its pictures stay the same: 35 lines, such as Window size's
  and Debug mode's helps, which are over four lines, and the stress values menutest uses on purpose, such as CRT
  colours' "BT.601 PAL (...) (not with this renderer)". The same rows in other languages overflow the same way.

## Tests

- **`langtest`** (`make build/bin/langtest`): the tables, the places, plain place names, every letter of every
  language in its fonts, the language names in their fonts, `tr::line` and `from_net`, `fill`, the setting's round trip
  (`language =` key ↔ choice for all 14 choices, an unknown code refused, Left and Right wrap), the locale mapping
  (zh-TW, zh-HK, zh-SG, zh-Hant, pt-PT and so on), and pseudo's length.
- **`menutest`** in any language (`VF_LANG=ko`):
  - every Online screen, the in-match menu, the notices, the VS screen, the watching cells;
  - replicas of every main.cpp page (`pages_*.png`), each row selected so each help is measured: Display (every
    value it can show), Sound, Game, Language's 14 choices, Arcade settings, Cheats (the headings, the seed's row,
    and a seed being typed with each of its mistakes under it), Controls, a player's keys and pad buttons, the
    prompts and the longest notices;
  - every line checked against its boxes;
  - the rows' left edges checked to be in line (`game_menu::misaligned`);
  - a runtime switch with a page open: en, then ko, ja, ru and tr, each drawn at once, then back to en, which must be
    the identical picture.
- **`tools/langcheck.py`**: menutest in all 13 languages plus pseudo, each compared with English. All 12 translations
  report 0 boxes overflowed that the English fits and 0 extra failed checks, with a lobby and without. Pseudo is
  listed, not failed: 287 tight spots, all in the replicas' stress values, the crumbs and the key-hint line. With
  `VF_LOBBY` set (Virtua Fighter 3 Recomp's tools/lobby, `test/serve.mjs`), the room screens are included, and
  langcheck runs one language at a time so that Quick match doesn't pair two of them.
- **The game itself, headless, per language** (`VF2_SCRIPT` scripted play for 3000 frames with `language = <code>`
  in settings.ini, the CPU's 3D on SDL's software renderer): the game's frames at 900, 1500, 2405, 2909 and 2990
  (the script's shots: what the board drew) are byte-identical in all 13 languages and pseudo, and the in-game menu
  is drawn in each. (`build/host_last.png`, the picture on show as the run quits, is a smooth-motion blend at the wall
  clock's moment: it differs between two English runs too, so it isn't compared.) Game > Language stepped through all 14 choices in the running game
  (`VF2_MENU_KEYS`) redraws the page in each at once.
- **Online**: the language isn't in any packet. nettest (24 cases), delaytest and watchsim (0, 1 and 50 watchers)
  pass as before, and menutest against a lobby (Virtua Fighter 3 Recomp's `node tools/lobby/test/serve.mjs`) passes
  all 115 checks in English (109 before, and the same 63 pictures). src/net isn't changed.

## Glossary and what needs a native speaker

The glossary, the main terms in all 13 languages side by side, is `docs/i18n-glossary.md`, made from the tables by
`tools/glossary.py`. The strings Virtua Fighter 2 shares with Virtua Fighter 3 (the online, room, watching, settings,
controls and net ones) are Virtua Fighter 3 Recomp's translations word for word, so the terms are the same in both
games; its docs/i18n-design.md lists, by language, what a native speaker should check first. Every translation here
was written without a native reviewer. Virtua Fighter 2's own strings, by language:

- **All languages**: the random fighters' words, Virtua Fighter 2's (and VF1's, when it has them) own: Normal / Random /
  Seed (a player's fighter), "random fighter" in prose, Random opponents, Secret character, Online > Settings > Your
  fighter. Seed is a random fighter's name that makes the same one anywhere: is the word below the one players would
  use, or should it stay "Seed"? Ring size and "ring out", Continue, After a versus match (the arcade's VS FINISH).
- **Korean**: 시드 (seed), 랜덤 캐릭터, 링 크기 / 링 아웃, 컨티뉴, 숨겨진 캐릭터. Fighters in prose as 아키라, 라이온, 듀랄 (as
  VF3's 듀랄); the values keep Dural, MUE, KOJAC. "시드가 {seed}인 캐릭터가 없어요": 인 after a place, fine after any
  noun?
- **Japanese**: シード, ランダムキャラクター, リングの広さ (標準 / 小 / 中 / 大 / 特大), コンティニュー, 隠しキャラクター,
  ティアリング (tearing). アキラ, ライオン, デュラル in prose. Shortened to fit: "Enterで入力（それまでランダム）" (the seed's row),
  "オフ（大きすぎ）", "CPU（GPU非対応ビルド）", "自動（パッドなし）".
- **Simplified Chinese**: 种子 (seed), 随机角色, 擂台大小 / 出界 (ring out; 场地 stays the stage), 续关 (Continue), 隐藏角色,
  撕裂 (tearing). Fighters' names in Latin letters (as VF3's Kage).
- **Traditional Chinese**: written by hand from the Simplified (no OpenCC here), Taiwan's terms: 種子, 隨機角色, 擂台, 接關
  (Continue: 續關 in Hong Kong?), 手把, 預設, 效能, 執行緒.
- **Spanish**: Semilla (seed), luchador aleatorio, ring / ring outs, Continuar, Sigue el ganador / Paran los dos.
  Shortened: "Enter para la semilla (mientras, Aleatorio)".
- **Brazilian Portuguese**: Semente, lutador aleatório, ringue / ring outs, Continue (kept), "Após uma partida
  versus" and "Vencedor segue" (shortened to fit the row), "Enter para a semente (Aleatório até lá)".
- **French**: Graine (seed), combattant aléatoire, ring / ring out, Continue (kept), Le gagnant continue / Les deux
  s'arrêtent, "Entrée", "Échap" and "Retour arrière" in the seed's key line (as VF3's Suppr).
- **Russian**: Сид (seed: or "Seed"?), случайный боец, ринг / ринг-аут, Стандарт (Default ring size), Продолжение
  (Continue), "Верт. синхронизация" (abbreviated), Akira, Lion, Dural in Latin letters.
- **German**: Seed (kept in English), Zufall / Zufallskämpfer, Ringgröße / Ring-outs, Continue (kept), Sieger spielt
  weiter / Beide hören auf, Eingabe / Rücktaste in the seed's key line.
- **Italian**: Seme (seed), lottatore casuale, Dimensione ring (Predefinita / Piccola / Media / Grande / Molto grande,
  feminine for dimensione), Continue (kept), Invio in the seed's key line.
- **Polish**: Ziarno (seed), losowa postać, Rozmiar ringu / wypchnięcia z ringu, Kontynuacja, Dural as "ostatnią
  przeciwniczkę" (the final opponent: VF3's "Bossowa" avoided), Normalna / Losowa agree with postać.
- **Turkish**: Tohum (seed), rastgele dövüşçü, Ring boyutu / ring dışına atmak, Devam (Continue). No suffix after a
  place: "Bu tohuma sahip dövüşçü yok: {seed}". Guard / Punch / Kick kept, as VF3's.

## Virtua Fighter 2's own, and what is shared

Shared with Virtua Fighter 1 and 3 Recomp, the same files in each (a change goes to all three):
- `src/host/i18n.h`, `i18n.cpp`, `text.h`, `text.cpp`;
- `tools/langtest.cpp`, `tools/langcheck.py`, `tools/fonts/subset.py`;
- the RmlUi changes, `src/host/menu.cpp` and `menu.h`'s language parts (`FALLBACK_FONTS`, `fallback_order()`,
  `set_language()`, `overflow()`, `misaligned()`, the note's auto-fit);
- `assets/fonts/OFL-NotoSans*.txt` and the README lines;
- the Online site page (in Virtua Fighter 3 Recomp).

Virtua Fighter 2's own:
- the tables: Virtua Fighter 3's, less its own rows (Team Battle, Dural on the select and her stage, the stage
  select, the energy, country and CPU order settings, the GPU renderer's lines it hasn't got), with Virtua Fighter 2's
  rows added: Display's Low antialiasing, GPU quality, Vsync, GPU frames in flight, Performance hints and On-screen
  controls; Arcade settings' Ring size, Continue, After a versus match and Version; Cheats (each player's fighter,
  Normal, Random or Seed, the seed typed in its row, Random opponents, the Secret character); Online > Settings >
  Your fighter, what a match plays, and the versus screen's RANDOM and SEED;
- the pages (main.cpp), the menutest replicas of them (`pages_*` in tools/menutest.cpp) and `tools/glossary.py`'s
  list of terms;
- its fonts, cut to its own strings.

## Adding Arabic, Hindi or Thai (not done)

- **Arabic** (and Persian, Hebrew) needs right to left:
  - RmlUi doesn't lay out RTL or shape Arabic. Each letter's form depends on its neighbours, and without HarfBuzz
    the letters come out unjoined.
  - It would need HarfBuzz shaping (or pre-shaped presentation forms, which are poor), a bidi pass (FriBidi or
    SheenBidi) to reorder mixed runs, since names, codes and numbers stay left to right, and mirrored layouts: the
    rows' values on the left, arrows reversed, the key hints' order.
  - Plus Noto Sans Arabic (about 100 KB subset).
- **Hindi** (Devanagari) needs shaping too: conjuncts, matras that reorder, the headline joined across letters.
  RmlUi's FreeType path draws one glyph per code point, so it would need HarfBuzz. Line breaking is at spaces,
  which RmlUi already does. Plus Noto Sans Devanagari (about 60 KB subset).
- **Thai** needs:
  - line breaking without spaces (a dictionary, e.g. ICU's or libthai's, since the CJK rule here doesn't apply:
    Thai breaks between words, not characters);
  - vowel and tone marks stacked above and below (positioned by HarfBuzz or the font's GPOS, which RmlUi doesn't
    apply);
  - Noto Sans Thai (about 30 KB).

So all three need HarfBuzz in RmlUi's font engine (RmlUi 6 has an optional HarfBuzz font engine plugin; switching to
it is the largest step). Arabic also needs bidi and a mirrored layout. Then the tables, fonts and tests work as for
any other language. The site page needs less: the browser shapes them all, so only `dir="rtl"` handling for Arabic
and the fonts.
