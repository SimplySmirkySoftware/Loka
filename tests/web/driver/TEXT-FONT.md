# Controlled text font

`make-font.py` builds an original five-glyph SFNT font under `build/`, using only
Python's standard library. The glyph outlines are rectangles; no third-party
font data is included. The generator and its font output are dedicated to the
public domain under CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/).

The Latin `a` and `x` map to a regular glyph with advance 500/1000 em. OpenType
`locl` substitutions select advances 600 for Japanese, 700 for simplified or
traditional Chinese, and 800 for Turkish. Browser cells compare `aaaa` between
canvas and element, require three distinct language advances as a positive
control, and repeat under polluted inherited typography and synthetic bold.
Only a regular face exists. Both targets substitute the fixture family after
applying the production Env; all other Env rows remain in effect.

The browser driver builds the font before starting Chromium. No font tool or
package installation is required. The generated binary is not committed.
