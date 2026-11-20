# Themes

Atlas ships ten themes. Four are its own: Kites Dark, Kites Light, Tokyo
Night and Tokyo Night Storm. Six are ports of Visual Studio Code colour
themes (added in T8). The ports translate each theme's colours into Atlas's
tokens (`resources/themes/*.json`). They contain no code from the original
extensions.

Settings > Preferences and the Preferences dialog list the dark themes
first, then the light ones, each sorted by name.

## The six VS Code themes

| Atlas theme | Kind | VS Code extension (version used) | Author | Licence |
|---|---|---|---|---|
| Cobalt2 Theme Official | dark | `wesbos.theme-cobalt2` 2.5.0, theme "Cobalt2" | Wes Bos, Roberto Achar | MIT |
| Night Owl | dark | `sdras.night-owl` 2.1.1, theme "Night Owl" | Sarah Drasner | MIT |
| Monokai Dark Soda | dark | `AdamCaviness.theme-monokai-dark-soda` 1.2.0 | Adam Caviness | MIT |
| Remedy | light | `robertrossmann.remedy` 5.28.0, theme "Remedy - Bright (Straight)" | Robert Rossmann | BSD-3-Clause |
| Zed Theme | light | `solidhelium.zed-theme` 0.0.2 | publisher solidhelium; licence holder Ike Ku | MIT |
| C++ Light Theme | light | `BogdanIonita.cxx-light-theme` 1.0.4 | Bogdan Ionita | MIT |

The colours were read from each extension's published VSIX file on the
Visual Studio Marketplace, from the theme JSON named above. None were
written from memory.

### How the colours were mapped

Each token is taken from a VS Code key where the theme defines one:

| Atlas token | VS Code source, in order of preference |
|---|---|
| surface | `editor.background` |
| background | `sideBar.background`, `activityBar.background`, `panel.background` |
| foreground | `editor.foreground` |
| mutedForeground | `descriptionForeground`, `editorLineNumber.foreground` |
| border | `editorGroup.border`, `sideBar.border`, `panel.border`, `focusBorder` |
| controlBorder | `input.border`, `dropdown.border`, `checkbox.border`, `button.border` |
| accent | `button.background`, `activityBarBadge.background`, `focusBorder` |
| selection | `editor.selectionBackground` |
| currentLine | `editor.lineHighlightBackground` |
| iconColor | `icon.foreground`, `foreground` |
| success / warning / error / info | `terminal.ansiGreen` / `ansiYellow` / `ansiRed` / `ansiBlue` |
| cacheHit | `terminal.ansiCyan` |
| cacheMiss | the same as error |

Colours with alpha are flattened over the colour beneath them.

The syntax colours come from `tokenColors`, using VS Code's longest-prefix
rule:

| Syntax token | Scope |
|---|---|
| r32Instruction | `keyword` |
| r64Instruction | `keyword.control`, `storage.type` |
| mInstruction and fInstruction | `support.function`, `entity.name.function` |
| dInstruction | `entity.name.type`, `support.type` |
| pseudoInstruction | `storage.type`, `storage`, `keyword.operator` |
| register | `variable.parameter`, `variable` |
| comment | `comment` |
| label | `entity.name.label`, `entity.name.tag`, `entity.name.function` |
| numberLiteral | `constant.numeric` |

**Derived tokens.** Where a theme defines no key for a token, the token is
derived from that theme's own colours by a stated rule (a blend, or the
theme's most saturated colour near a hue). The processor diagram's tokens
(`colors.diagram`) are always derived, because VS Code has no diagram.
House themes leave `diagram` out and keep the original black diagram.

**Readability adjustments.** Where a colour misses one of the readability
checks in `test/theme_catalogue_test.cpp`, only its lightness was changed,
in 1% steps, until it passes. Every derivation and every adjustment is
listed below, from the generator's log.

### Readability rules (`ThemeCatalogue.*` tests)

- **Main text and icons:** 4.5:1 on background, surface and the current
  line. Main text also 4.5:1 on the selection.
- **Secondary text:** 3:1 on background, surface and the current line.
- **Control outlines (`controlBorder`):** 3:1 on background and surface.
  - The thin separator line (`border`) is decorative and exempt, as in the
    house themes.
  - House themes have no `controlBorder` and draw controls with `border`
    (1.1 to 1.4:1). This is a known exception, kept so their look is
    unchanged.
- **Timing grid:**
  - every stage tint is visibly off the surface (ΔE76 ≥ 10);
  - cell text is dark on pale cells in light themes, and light on dark
    cells in dark themes;
  - the flush cross is 3:1 on every tint, and the bubble hatching 1.5:1;
  - the five stage colours differ (ΔE76 ≥ 15). Known exception: Kites
    Light, Tokyo Night and Tokyo Night Storm give ID and MEM the same
    colour (`success` = `cacheHit`).
- **Syntax colours (new themes):** 3:1 on the editor background and the
  current line.

### Derivation log

```
== Cobalt2 Theme Official (cobalt2-theme-official, dark) from wesbos.theme-cobalt2/x/extension/theme/cobalt2.json
  surface            #193549  <- editor.background
  background         #15232d  <- sideBar.background
  foreground         #ffffff  <- editor.foreground
  mutedForeground    #aaaaaa  <- descriptionForeground
  border             #122738  <- editorGroup.border
  controlBorder      #0d3a58  <- input.border
  accent             #0088ff  <- button.background
  selection          #0050a4  <- editor.selectionBackground
  currentLine        #1f4662  <- editor.lineHighlightBackground
  iconColor          #aaaaaa  <- foreground
  success            #3ad900  <- terminal.ansiGreen
  warning            #ffc600  <- terminal.ansiYellow
  error              #ff628c  <- terminal.ansiRed
  info               #0088ff  <- terminal.ansiBlue
  cacheHit           #80fcff  <- terminal.ansiCyan (not success: the MEM stage must not share the ID stage's colour)
  cacheMiss          #ff628c  <- error
  syntax.r32Instruction   #ff9d00  <- tokenColors "keyword"
  syntax.r64Instruction   #ff9d00  <- tokenColors "keyword" (for keyword.control)
  syntax.mInstruction     #ff9d00  <- tokenColors "support.function"
  syntax.fInstruction     #ff9d00  <- tokenColors "support.function" (as mInstruction, like the house themes)
  syntax.dInstruction     #ffc600  <- tokenColors "entity" (for entity.name.type)
  syntax.pseudoInstruction #ffc600  <- tokenColors "storage"
  syntax.register         #e1efff  <- tokenColors "variable"
  syntax.comment          #0088ff  <- tokenColors "comment"
  syntax.label            #9effff  <- tokenColors "entity.name.tag"
  syntax.numberLiteral    #ff628c  <- tokenColors "constant"
    ADJUSTED iconColor: #aaaaaa -> #afafaf (to clear 4.5:1 against #15232d, #193549, #1f4662; was 4.28, now 4.54)
    ADJUSTED controlBorder: #0d3a58 -> #1d83c7 (to clear 3.0:1 against #15232d, #193549; was 1.07, now 3.11)
    ADJUSTED error: #ff628c -> #ff90ae (to clear 3.0:1 against #144771, #205939, #4c5539, #306171, #4c6171; was 2.27, now 3.03)
    ADJUSTED syntax.comment: #0088ff -> #0f8fff (to clear 3.0:1 against #193549, #1f4662; was 2.83, now 3.03)
  diagram.background      #193549  <- DERIVED: editor.background
  diagram.component       #3f4b53  <- DERIVED: 18% foreground over background
  diagram.componentBorder #aaaaaa  <- DERIVED: mutedForeground
  diagram.label           #ffffff  <- DERIVED: foreground
  diagram.wire            #707b83  <- DERIVED: mutedForeground 60% mutedForeground over editor.background (an idle wire, fainter than a lit one)
  diagram.wireActive      #ffc600  <- DERIVED: warning (the theme's yellow/orange: a lit wire, as the default yellow)
  -> cobalt2_theme_official.json
== Night Owl (night-owl, dark) from sdras.night-owl/x/extension/themes/Night Owl-color-theme.json
  surface            #011627  <- editor.background
  background         #011627  <- sideBar.background
  foreground         #d6deeb  <- editor.foreground
  mutedForeground    #4b6479  <- editorLineNumber.foreground
  border             #213444  <- DERIVED: 15% editor.foreground over background (a faint separator, as the house themes)
  controlBorder      #5f7e97  <- input.border
  accent             #654aa3  <- button.background #7e57c2cc over #011627
  selection          #1d3b53  <- editor.selectionBackground
  currentLine        #072435  <- editor.lineHighlightBackground #28707d29 over #011627
  iconColor          #d6deeb  <- foreground
  success            #22da6e  <- terminal.ansiGreen
  warning            #c5e478  <- terminal.ansiYellow
  error              #ef5350  <- terminal.ansiRed
  info               #82aaff  <- terminal.ansiBlue
  cacheHit           #21c7a8  <- terminal.ansiCyan (not success: the MEM stage must not share the ID stage's colour)
  cacheMiss          #ef5350  <- error
  syntax.r32Instruction   #c792ea  <- tokenColors "keyword"
  syntax.r64Instruction   #7fdbca  <- tokenColors "keyword.control"
  syntax.mInstruction     #82aaff  <- tokenColors "support.function"
  syntax.fInstruction     #82aaff  <- tokenColors "support.function" (as mInstruction, like the house themes)
  syntax.dInstruction     #c5e478  <- tokenColors "support.type"
  syntax.pseudoInstruction #c5e478  <- tokenColors "support.type" (storage.type equals keyword)
  syntax.register         #ffcb8b  <- tokenColors "variable.parameter"
  syntax.comment          #637777  <- tokenColors "comment"
  syntax.label            #caece6  <- tokenColors "entity.name.tag"
  syntax.numberLiteral    #f78c6c  <- tokenColors "constant.numeric"
    ADJUSTED mutedForeground: #4b6479 -> #536e86 (to clear 3.0:1 against #011627, #011627, #072435; was 2.59, now 3.00)
    ADJUSTED accent: #654aa3 -> #6e51b0 (to clear 3.0:1 against #011627, #011627; was 2.67, now 3.03)
    ADJUSTED error: #ef5350 -> #ef5855 (to clear 3.0:1 against #1d3757, #084137, #2c4339, #083d43, #304252; was 2.97, now 3.06)
  diagram.background      #011627  <- DERIVED: editor.background
  diagram.component       #273a4a  <- DERIVED: 18% foreground over background
  diagram.componentBorder #536e86  <- DERIVED: mutedForeground
  diagram.label           #d6deeb  <- DERIVED: foreground
  diagram.wire            #324b60  <- DERIVED: mutedForeground 60% mutedForeground over editor.background (an idle wire, fainter than a lit one)
  diagram.wireActive      #c5e478  <- DERIVED: warning (the theme's yellow/orange: a lit wire, as the default yellow)
  -> night_owl.json
== Monokai Dark Soda (monokai-dark-soda, dark) from AdamCaviness.theme-monokai-dark-soda/x/extension/themes/monokai-dark-soda-color-theme.json
  surface            #242424  <- editor.background
  background         #1f1f1f  <- DERIVED: editor.background darkened 15% (theme defines no side bar/panel colour)
  foreground         #f8f8f2  <- editor.foreground
  mutedForeground    #a3a3a0  <- DERIVED: 60% editor.foreground over editor.background
  border             #40403f  <- DERIVED: 15% editor.foreground over background (a faint separator, as the house themes)
  controlBorder      #8e8e8b  <- DERIVED: 50% editor.foreground over editor.background
  accent             #f92672  <- DERIVED: the keyword colour, tokenColors "keyword"
  selection          #403d3d  <- editor.selectionBackground
  currentLine        #3d3d3d  <- editor.lineHighlightBackground
  iconColor          #f8f8f2  <- editor.foreground
  success            #60cf30  <- DERIVED: the theme's most saturated colour within 40 deg of hue 120
  warning            #fab85a  <- DERIVED: the theme's most saturated colour within 40 deg of hue 40
  error              #f44747  <- DERIVED: the theme's most saturated colour within 40 deg of hue 0
  info               #6796e6  <- DERIVED: the theme's most saturated colour within 40 deg of hue 215
  cacheHit           #90e7f7  <- DERIVED: the theme's most saturated colour within 40 deg of hue 185 (not success: the MEM stage must not share the ID stage's colour)
  cacheMiss          #f44747  <- error
  syntax.r32Instruction   #f92672  <- tokenColors "keyword"
  syntax.r64Instruction   #66d9ef  <- tokenColors "storage.type"
  syntax.mInstruction     #66d9ef  <- tokenColors "support.function"
  syntax.fInstruction     #66d9ef  <- tokenColors "support.function" (as mInstruction, like the house themes)
  syntax.dInstruction     #66d9ef  <- tokenColors "support.type"
  syntax.pseudoInstruction #66d9ef  <- tokenColors "storage.type"
  syntax.register         #fd971f  <- tokenColors "variable.parameter"
  syntax.comment          #8c8c8c  <- tokenColors "comment"
  syntax.label            #f92672  <- tokenColors "entity.name.tag"
  syntax.numberLiteral    #ff80f4  <- tokenColors "constant.numeric"
    ADJUSTED error: #f44747 -> #f78181 (to clear 3.0:1 against #333d4f, #314a27, #534530, #3c4f52, #535351; was 2.14, now 3.09)
    ADJUSTED syntax.r32Instruction: #f92672 -> #f9357c (to clear 3.0:1 against #242424, #3d3d3d; was 2.87, now 3.01)
    ADJUSTED syntax.label: #f92672 -> #f9357c (to clear 3.0:1 against #242424, #3d3d3d; was 2.87, now 3.01)
  diagram.background      #242424  <- DERIVED: editor.background
  diagram.component       #464645  <- DERIVED: 18% foreground over background
  diagram.componentBorder #a3a3a0  <- DERIVED: mutedForeground
  diagram.label           #f8f8f2  <- DERIVED: foreground
  diagram.wire            #70706e  <- DERIVED: mutedForeground 60% mutedForeground over editor.background (an idle wire, fainter than a lit one)
  diagram.wireActive      #fab85a  <- DERIVED: warning (the theme's yellow/orange: a lit wire, as the default yellow)
  -> monokai_dark_soda.json
== Remedy (remedy, light) from robertrossmann.remedy/x/extension/themes/bright-color-theme.json
  surface            #fef8eb  <- editor.background
  background         #fceed1  <- sideBar.background
  foreground         #563d0e  <- editor.foreground
  mutedForeground    #563d0e  <- descriptionForeground
  border             #e4c88d  <- editorGroup.border
  controlBorder      #e4c88d  <- input.border
  accent             #eb684b  <- button.background
  selection          #e8dfce  <- editor.selectionBackground #563D0E22 over #fef8eb
  currentLine        #f8f1e3  <- editor.lineHighlightBackground #563D0E09 over #fef8eb
    ADJUSTED currentLine: #f8f1e3 -> #f5ebd7 (to clear 1.1:1 against #fef8eb; was 1.06, now 1.12)
  iconColor          #563d0e  <- icon.foreground
  success            #8c9440  <- terminal.ansiGreen
  warning            #de935f  <- terminal.ansiYellow
  error              #a54242  <- terminal.ansiRed
  info               #5f819d  <- terminal.ansiBlue
  cacheHit           #5e8d87  <- terminal.ansiCyan (not success: the MEM stage must not share the ID stage's colour)
  cacheMiss          #a54242  <- error
  syntax.r32Instruction   #85678f  <- tokenColors "keyword"
  syntax.r64Instruction   #cc6666  <- tokenColors "storage.type"
  syntax.mInstruction     #5f819d  <- tokenColors "support.function"
  syntax.fInstruction     #5f819d  <- tokenColors "support.function" (as mInstruction, like the house themes)
  syntax.dInstruction     #cc6666  <- tokenColors "entity.name.type"
  syntax.pseudoInstruction #cc6666  <- tokenColors "storage.type"
  syntax.register         #563d0e  <- tokenColors "variable"
  syntax.comment          #af9054  <- tokenColors "comment"
  syntax.label            #5e8d87  <- tokenColors "entity.name.label"
  syntax.numberLiteral    #de935f  <- tokenColors "constant.numeric"
    ADJUSTED controlBorder: #e4c88d -> #ab8128 (to clear 3.0:1 against #fceed1, #fef8eb; was 1.41, now 3.10)
    ADJUSTED accent: #eb684b -> #e95c3d (to clear 3.0:1 against #fceed1, #fef8eb; was 2.77, now 3.02)
    ADJUSTED success: #8c9440 -> #858d3d (to clear 3.0:1 against #fef8eb, #fceed1; was 2.85, now 3.11)
    ADJUSTED warning: #de935f -> #d06e2b (to clear 3.0:1 against #fef8eb, #fceed1; was 2.17, now 3.08)
    ADJUSTED warning: #d06e2b -> #a85a21 (bubble hatching to clear 1.5:1 on every stage tint)
    ADJUSTED syntax.comment: #af9054 -> #9f824a (to clear 3.0:1 against #fef8eb, #f5ebd7; was 2.56, now 3.08)
    ADJUSTED syntax.numberLiteral: #de935f -> #cc6c2a (to clear 3.0:1 against #fef8eb, #f5ebd7; was 2.10, now 3.09)
  diagram.background      #fef8eb  <- DERIVED: editor.background
  diagram.component       #deceae  <- DERIVED: 18% foreground over background
  diagram.componentBorder #563d0e  <- DERIVED: mutedForeground
  diagram.label           #563d0e  <- DERIVED: foreground
  diagram.wire            #998866  <- DERIVED: mutedForeground 60% mutedForeground over editor.background (an idle wire, fainter than a lit one)
  diagram.wireActive      #85678f  <- DERIVED: the keyword colour (a strong ink on the pale page)
  -> remedy.json
== Zed Theme (zed-theme, light) from solidhelium.zed-theme/x/extension/themes/Zed Theme-color-theme.json
  surface            #f6f5f0  <- editor.background
  background         #f6f5f0  <- sideBar.background
  foreground         #000000  <- editor.foreground
  mutedForeground    #a2b9e4  <- editorLineNumber.foreground #1051d05d over #f6f5f0
  border             #e7e7e7  <- editorGroup.border
  controlBorder      #7b7a78  <- DERIVED: 50% editor.foreground over editor.background
  accent             #1051d0  <- button.background
  selection          #add6ff  <- editor.selectionBackground
  currentLine        #f6f5f1  <- editor.lineHighlightBackground #ffffff0A over #f6f5f0
    ADJUSTED currentLine: #f6f5f1 -> #eceae1 (to clear 1.1:1 against #f6f5f0; was 1.00, now 1.10)
  iconColor          #1051d0  <- icon.foreground
  success            #00bc00  <- terminal.ansiGreen
  warning            #949800  <- terminal.ansiYellow
  error              #cd3131  <- terminal.ansiRed
  info               #0451a5  <- terminal.ansiBlue
  cacheHit           #0598bc  <- terminal.ansiCyan (not success: the MEM stage must not share the ID stage's colour)
  cacheMiss          #cd3131  <- error
  syntax.r32Instruction   #a406d3  <- tokenColors "keyword"
  syntax.r64Instruction   #a406d3  <- tokenColors "keyword.control"
  syntax.mInstruction     #007e8e  <- tokenColors "support.function"
  syntax.fInstruction     #007e8e  <- tokenColors "support.function" (as mInstruction, like the house themes)
  syntax.dInstruction     #f59f00  <- tokenColors "entity.name.type"
  syntax.pseudoInstruction #f59f00  <- tokenColors "entity.name.type" (storage.type equals keyword)
  syntax.register         #d90819  <- tokenColors "variable"
  syntax.comment          #7f848e  <- tokenColors "comment"
  syntax.label            #d90819  <- tokenColors "entity.name.tag"
  syntax.numberLiteral    #cf6806  <- tokenColors "constant.numeric"
    ADJUSTED mutedForeground: #a2b9e4 -> #5f86d0 (to clear 3.0:1 against #f6f5f0, #f6f5f0, #eceae1; was 1.64, now 3.00)
    ADJUSTED success: #00bc00 -> #00a200 (to clear 3.0:1 against #f6f5f0, #f6f5f0; was 2.35, now 3.12)
    ADJUSTED warning: #949800 -> #8f9300 (to clear 3.0:1 against #f6f5f0, #f6f5f0; was 2.85, now 3.03)
    ADJUSTED error: #cd3131 -> #c12e2e (to clear 3.0:1 against #c1d1e0, #c0e3bb, #dfdfbb, #c1e1e5, #c0bfbb; was 2.80, now 3.09)
    ADJUSTED warning: #8f9300 -> #535700 (bubble hatching to clear 1.5:1 on every stage tint)
    ADJUSTED syntax.dInstruction: #f59f00 -> #b87700 (to clear 3.0:1 against #f6f5f0, #eceae1; was 1.77, now 3.07)
    ADJUSTED syntax.pseudoInstruction: #f59f00 -> #b87700 (to clear 3.0:1 against #f6f5f0, #eceae1; was 1.77, now 3.07)
  diagram.background      #f6f5f0  <- DERIVED: editor.background
  diagram.component       #cac9c5  <- DERIVED: 18% foreground over background
  diagram.componentBorder #5f86d0  <- DERIVED: mutedForeground
  diagram.label           #000000  <- DERIVED: foreground
  diagram.wire            #9bb2dd  <- DERIVED: mutedForeground 60% mutedForeground over editor.background (an idle wire, fainter than a lit one)
  diagram.wireActive      #a406d3  <- DERIVED: the keyword colour (a strong ink on the pale page)
    ADJUSTED diagram.wire: #9bb2dd -> #97afdc (to clear 2.0:1 against #f6f5f0; was 1.96, now 2.03)
  -> zed_theme.json
== C++ Light Theme (cpp-light-theme, light) from BogdanIonita.cxx-light-theme/x/extension/themes/C++ Light Theme-color-theme.json
  surface            #ffffff  <- editor.background
  background         #f5f5f5  <- DERIVED: editor.background darkened 4% (theme defines no side bar/panel colour)
  foreground         #000000  <- editor.foreground
  mutedForeground    #666666  <- DERIVED: 60% editor.foreground over editor.background
  border             #d0d0d0  <- DERIVED: 15% editor.foreground over background (a faint separator, as the house themes)
  controlBorder      #808080  <- DERIVED: 50% editor.foreground over editor.background
  accent             #007acc  <- activityBarBadge.background
  selection          #bfdef2  <- DERIVED: 25% accent over editor.background
  currentLine        #fffcec  <- editor.lineHighlightBackground
    ADJUSTED currentLine: #fffcec -> #fff5be (to clear 1.1:1 against #ffffff; was 1.03, now 1.10)
  iconColor          #000000  <- editor.foreground
  success            #008000  <- DERIVED: the theme's most saturated colour within 40 deg of hue 120
  warning            #cd9731  <- DERIVED: the theme's most saturated colour within 40 deg of hue 40
  error              #800000  <- DERIVED: the theme's most saturated colour within 40 deg of hue 0
  info               #0451a5  <- DERIVED: the theme's most saturated colour within 40 deg of hue 215
  cacheHit           #267f99  <- DERIVED: the theme's most saturated colour within 40 deg of hue 185 (not success: the MEM stage must not share the ID stage's colour)
  cacheMiss          #800000  <- error
  syntax.r32Instruction   #0000ff  <- tokenColors "keyword"
  syntax.r64Instruction   #0000ff  <- tokenColors "keyword.control"
  syntax.mInstruction     #8800d1  <- tokenColors "support.function"
  syntax.fInstruction     #8800d1  <- tokenColors "support.function" (as mInstruction, like the house themes)
  syntax.dInstruction     #267f99  <- tokenColors "entity.name.type"
  syntax.pseudoInstruction #267f99  <- tokenColors "support.type" (storage.type equals keyword)
  syntax.register         #001080  <- tokenColors "variable"
  syntax.comment          #008000  <- tokenColors "comment"
  syntax.label            #000000  <- tokenColors "entity.name.label"
  syntax.numberLiteral    #098658  <- tokenColors "constant.numeric"
    ADJUSTED warning: #cd9731 -> #b4852b (to clear 3.0:1 against #ffffff, #f5f5f5; was 2.39, now 3.04)
    ADJUSTED warning: #b4852b -> #70521a (bubble hatching to clear 1.5:1 on every stage tint)
  diagram.background      #ffffff  <- DERIVED: editor.background
  diagram.component       #c9c9c9  <- DERIVED: 18% foreground over background
  diagram.componentBorder #666666  <- DERIVED: mutedForeground
  diagram.label           #000000  <- DERIVED: foreground
  diagram.wire            #a3a3a3  <- DERIVED: mutedForeground 60% mutedForeground over editor.background (an idle wire, fainter than a lit one)
  diagram.wireActive      #0000ff  <- DERIVED: the keyword colour (a strong ink on the pale page)
  -> cpp_light_theme.json
```

## Licences

The licence texts below are reproduced from each extension's package, as
their terms require.

### Cobalt2 Theme Official (wesbos.theme-cobalt2)

```
MIT License

Copyright (c) 2018 Wes Bos, Roberto Achar

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Night Owl (sdras.night-owl)

```
MIT License

Copyright (c) 2018 Sarah Drasner

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Monokai Dark Soda (AdamCaviness.theme-monokai-dark-soda)

```
MIT License

Copyright (c) 2019 Adam Caviness

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Remedy (robertrossmann.remedy)

```
BSD 3-Clause License

Copyright (c) 2019, Robert Rossmann
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Zed Theme (solidhelium.zed-theme)

```
MIT License

Copyright (c) 2016 Ike Ku

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### C++ Light Theme (BogdanIonita.cxx-light-theme)

```
MIT License

Copyright (c) 2020 Bogdan Ionita

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

