/* termbox2's compile-time options, in the one place everything that reads the
 * header goes through.
 *
 * ==Why this file exists==
 *
 * **A header library's compile-time options are part of its ABI, and there are
 * now TWO translation units that include it.** `termbox2.c` compiles the
 * implementation, and the `c const` block in `c.sysl` is measured from a probe
 * translation unit of the compiler's own making (`15 §7`) -- a second file, with
 * its own preprocessor state, that includes what `@include` names.
 *
 * `TB_OPT_ATTR_W` decides both the width of `uintattr_t` and the *values* of the
 * twelve style attributes: `TB_BOLD` is `0x0100` at width 16 and `0x01000000` at
 * 32 or 64, and `TB_STRIKEOUT`, `TB_UNDERLINE_2`, `TB_OVERLINE` and
 * `TB_INVISIBLE` are not defined at all below 64. So a probe that included
 * `termbox2.h` directly would fail outright on four of them and measure eight
 * others as numbers the implementation does not use -- which is a mismatch that
 * compiles, links, and paints the wrong attribute at run time.
 *
 * Naming this file in both places makes that impossible rather than merely
 * unlikely. It is also the reason the option is not written in `termbox2.c` any
 * more: one place, or the ABI is a coincidence.
 *
 * ==The options, and why these values==
 *
 * `TB_OPT_ATTR_W 64` is the widest setting, and it is what buys
 * `TB_OUTPUT_TRUECOLOR` and the four style attributes above the first eight --
 * strikeout, double underline, overline and invisible. It costs sixteen bytes per
 * cell against the default of 16 bits. A binding has to choose once for
 * everybody, and a consumer who finds a colour missing has no way to recompile
 * the package; a consumer who finds a terminal buffer larger than they wanted
 * still has a working program.
 *
 * `TB_OPT_EGC` is deliberately NOT set. It would add three fields to every cell
 * and enable `tb_set_cell_ex` and `tb_extend_cell`, which take an array of
 * codepoints making up one grapheme cluster. sysl's text layer does not compute
 * grapheme clusters -- `sysl.text` is explicit that clustering is ASCII-only for
 * now -- so the binding would be offering a parameter its own standard library
 * cannot produce. Wide characters do not need it: termbox handles a `wcwidth==2`
 * codepoint by zeroing the cell after it, with EGC off.
 *
 * `TB_OPT_LIBC_WCHAR` is also not set, and that one is load-bearing. Left unset,
 * termbox measures display width with its own Unicode tables. Set, it uses libc's
 * `wcwidth(3)`, which answers -1 for every non-ASCII codepoint until the program
 * has called `setlocale(3)` with a UTF-8 locale. A library that silently required
 * its consumer to call `setlocale` would be a library that lays out CJK text
 * correctly on the author's machine and not on the reader's.
 *
 * **All three are reported at run time and asserted by the tests** --
 * `tb_attr_width`, `tb_has_truecolor` and `tb_has_egc` -- because a `#define` is
 * invisible to the linker and those assertions are the only thing standing
 * between a mismatch and a silent one.
 */
#ifndef SYSL_TB_OPTIONS_H
#define SYSL_TB_OPTIONS_H

#define TB_OPT_ATTR_W 64

#include "termbox2.h"

#endif
