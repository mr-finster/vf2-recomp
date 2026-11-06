# Third-party code

- `mame/`: MAME's i960 core and disassembler and its 68000, BSD-3-Clause (see mame/README.md for what else was ported from MAME).
- `sljit/`: Zoltan Herczeg's SLJIT (BSD-2-Clause, see sljit/LICENSE; the commit in sljit/VERSION), as
  vf1-recomp has it: the native code the i960 program is recompiled to when the game starts (src/rec).
- `rmlui/`: RmlUi 6.3 (MIT, see rmlui/LICENSE.txt), the in-game menu: Include, Source/Core and the SDL
  platform and renderer backends, as vf1-recomp has them. Three changes: RmlUi_Renderer_SDL.cpp loads images
  with SDL 3.4's own SDL_LoadSurface_IO (BMP, PNG) instead of SDL_image, has BeginOverlay(): BeginFrame without
  the clear, and uses SDL3's built-in premultiplied blend (the same blend as its custom one, which the software
  renderer doesn't support). For the menus' languages, as VF3 has it: fallback faces ordered by language, then style
  and weight (`Rml::SetFallbackFontOrder`, FontProvider, FontFaceHandleDefault), and lines broken between Chinese,
  Japanese and Korean letters with the kinsoku rules (ElementText's BuildToken).
- `stb/`: Sean Barrett's stb_image 2.30 (public domain / MIT, see the end of the file), for loading the
  widescreen's painted sides (PNG, JPG).

The menu's typeface, Barlow Condensed, is in assets/fonts (SIL Open Font License 1.1, assets/fonts/OFL.txt).
