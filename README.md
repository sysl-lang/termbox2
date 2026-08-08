# termbox2

A full-screen terminal interface for [sysl](https://github.com/sysl-lang/sysl) — cells, colours,
keys and the mouse, from a single header carried in the package.

**Nothing has to be installed to use this.** termbox2 is a single-header library and this package
carries the header: sysl compiles a library's C as part of the build, so there is no `-l` flag, no
`pkg-config`, and no build script anywhere in this repository. That is also why there is no `@link`
in the binding's header — there is no external library to name.

```
sh/sysl/termbox2/
    termbox2.sysl       the binding
    termbox2.c          the implementation and the shim, in one translation unit
    termbox2.h          vendored from termbox/termbox2
    tests.sysl          21 tests, none of which needs a terminal
package.hocon           who this package is, and what it needs of the machine
```

The module is **`sh.sysl.termbox2`**, and the three directories are that name: a dotted module name
mirrors its path from the library root. The prefix is the reverse-DNS of `sysl.sh`, so that a package
claims a name nobody else will mint rather than the top-level word `termbox2`.

## Using it

Name it in your project's `package.hocon` and `sysl build` fetches it:

```hocon
dependencies {
  termbox2 { git = "github.com/sysl-lang/termbox2", version = "0.1.1" }
}
```

The coordinate is an identity rather than a URL, so it carries no `https://`, and `version` is the
tag `v0.1.1` here.

Or point at it directly, which needs no fetching and is what this repository's own tests do. Either a
built artifact or the source tree works, and they are the same road:

```
sysl build-lib . -o /tmp/termbox2.syslib
sysl run yourprogram.sysl --lib /tmp/termbox2.syslib

sysl run yourprogram.sysl --lib /path/to/this/repo
```

## An example

A package has no way to carry a program of its own — everything under the package root is compiled
*into* the library — so the demo lives here (`design/packages.md § Open h`).

```sysl
import sh.sysl.termbox2.*

// Redraw everything. A termbox program rewrites every cell it cares about and lets `present` work
// out what actually changed, rather than tracking that itself.
draw(w: int, h: int, said: string) -> Result[unit, Error]
    clear()?

    for x in 0..<w
        set_cell(x, 0, u32('-'), CYAN, DEFAULT)?
        set_cell(x, h - 1, u32('-'), CYAN, DEFAULT)?

    print_at(2, 2, YELLOW | BOLD, DEFAULT, "termbox2, from sysl")?
    print_at(2, 4, DEFAULT, DEFAULT, f"the screen is ${w} by ${h}")?
    print_at(2, 5, DEFAULT, DEFAULT, said)?
    print_at(2, h - 3, DEFAULT, DEFAULT, "q or Escape to leave")?

    present()
end draw

main() -> Result[unit, Error]
    init()?

    var said = "press something"
    var going = true

    while going
        draw(width()?, height()?, said)?

        val ev = poll_event()?

        ev.kind match
            Resize   -> said = f"resized to ${ev.w} by ${ev.h}"
            Mouse    -> said = f"${ev.key} at ${ev.x}, ${ev.y}"
            Keyboard ->
                if ev.key == Esc || ev.ch == u32('q') then
                    going = false
                elif ev.ch != 0 then
                    said = f"you typed U+${ev.ch}%04X"
                else
                    said = f"you pressed ${ev.key}"
            UnknownEvent -> said = "something termbox had no name for"

    shutdown()
```

**`shutdown` restores the terminal, and a program that leaves without it leaves the terminal
unusable** — raw mode on, the cursor hidden, the alternate screen still in front of the reader's
shell, and no echo, so they cannot see what they are typing to fix it. It belongs on every path out,
which is why the loop above ends by falling out rather than by returning from the middle.

## The surface

```sysl
init() / init_file(path) / init_fd(fd) / init_rwfd(rfd, wfd) / shutdown()

width() / height()
clear() / set_clear_attrs(fg, bg) / present() / invalidate()
set_cursor(x, y) / hide_cursor()

set_cell(x, y, ch, fg, bg)
print_at(x, y, fg, bg, s) -> usize      // columns taken
send(bytes)                             // raw, around the cell buffer

set_input_mode(InputMode) / input_mode()
set_output_mode(OutputMode) / output_mode()

poll_event() -> Event
peek_event(timeout_ms) -> Option[Event]
fds() -> (int, int)                     // for a program running its own select loop

version() / attr_width() / has_truecolor() / has_egc()
wcwidth(ch) / is_printable(ch) / key_code(Key) / last_errno()
```

**Every call answers a `Result`, because a terminal is a resource rather than a canvas** — it may not
exist, it may not be one termbox knows, and a write to it may fail. A call before `init` answers
`NotInit` rather than doing nothing quietly.

`Error` carries termbox's own number, renders as termbox's own sentence, and matches as an
`ErrorKind`. For the failures that carry an `errno` — `Failed`, `InitOpen`, `ReadFailed`,
`PollFailed`, `TcGetAttr`, `TcSetAttr` and most of the `Resize*` family — the sentence is the C
library's `strerror` text and `Error.errno()` is the number behind it.

### Colours are integers, and styles are bits on top

```sysl
set_cell(x, y, u32('@'), RED | BOLD | UNDERLINE, DEFAULT)
```

`DEFAULT`, `BLACK`, `RED`, `GREEN`, `YELLOW`, `BLUE`, `MAGENTA`, `CYAN`, `WHITE` are the colours;
`BOLD`, `UNDERLINE`, `REVERSE`, `ITALIC`, `BLINK`, `HI_BLACK`, `BRIGHT`, `DIM`, `STRIKEOUT`,
`UNDERLINE_2`, `OVERLINE`, `INVISIBLE` are the attributes. All twelve attributes exist because the
package is built at 64-bit attribute width; see below.

What the *numbers* mean depends on the output mode — `RED` in `Normal`, an index 0..255 in
`Color256`, `0xRRGGBB` in `Truecolor`, which `rgb(r, g, b)` builds. **Cell attributes are not
translated when the mode changes**, so a program that switches has to redraw, and `invalidate` is
what makes the switch take effect everywhere at once.

### Keys and characters are alternatives

`Event.key` and `Event.ch` never both carry the keystroke. A printable character arrives as `ch` with
`key` at `Nul`; an arrow, a function key, Escape or a control key arrives as `key` with `ch` at zero.

**Several keys share one byte, because the terminal sends one byte for what the reader thinks are two
keys.** Backspace and Ctrl-H are both 0x08, Tab and Ctrl-I both 0x09, Enter and Ctrl-M both 0x0d,
Escape and Ctrl-[ both 0x1b. Only one name can come back for one byte; the binding keeps the one a
reader is likelier to have meant and says beside each variant what was lost. **And most terminals
send `Backspace2` (0x7f) for the backspace key**, so a program handling only `Backspace` handles it
almost nowhere.

`ctrl` and `shift` are only ever set on the arrow keys, and `alt` only in `InputMode.alt` — a control
key elsewhere arrives as a `Ctrl*` variant rather than as a modifier, which is the terminal's doing
and not termbox's.

## What is not bound, and why

| absent | why |
|---|---|
| `tb_printf`, `tb_printf_ex`, `tb_sendf` | Varargs, which sysl does not spell — and does not need to, since `f"…"` builds the string first. |
| `tb_set_cell_ex`, `tb_extend_cell` | Grapheme clusters, which need `TB_OPT_EGC`. The package does not set it; see below. |
| `tb_get_cell`, `tb_cell_buffer` | Reading the cell grid back. `tb_cell_buffer` is deprecated upstream, and `tb_get_cell` hands out a pointer into termbox's storage that the next call may free. |
| `tb_set_func` | A C function pointer, and deprecated upstream. |
| `tb_utf8_*` | `sysl.text` already decodes UTF-8, and two answers to one question is one too many. |

## The choices the package made for you

A single-header library's compile-time options are chosen once by whoever compiles it, and a consumer
of this package cannot recompile it. So the three that matter are set here, and each is a real
decision rather than a default left alone.

- **`TB_OPT_ATTR_W` is 64**, the widest. It is what buys `Truecolor` and the four style attributes
  above the first eight, and it costs sixteen bytes per cell against the default of 16 bits. A
  consumer who finds a colour missing has no recourse; one who finds a buffer larger than they wanted
  still has a working program.
- **`TB_OPT_EGC` is off.** It would enable `tb_set_cell_ex` and `tb_extend_cell`, which take an array
  of codepoints making up one grapheme cluster — and `sysl.text` does not compute grapheme clusters
  yet, so the binding would be offering a parameter its own standard library cannot produce. Wide
  characters do not need it: termbox handles a two-column codepoint by zeroing the cell after it.
- **`TB_OPT_LIBC_WCHAR` is off**, and this one is load-bearing. Left off, termbox measures display
  width with its own Unicode tables. Set, it would use libc's `wcwidth(3)`, which answers -1 for
  every non-ASCII codepoint until the program has called `setlocale(3)` with a UTF-8 locale — and no
  sysl program does. A package built the other way lays CJK out one column wide.

`attr_width()`, `has_truecolor()` and `has_egc()` report all three at run time, which is the only way
they can be checked at all: a `#define` is invisible to the linker, so a mismatch between what the
header was compiled with and what a caller assumes would compile, link, and pass the wrong number of
bytes for every colour. The tests assert each of them.

### One divergence from upstream

**`send` answers `NotInit` before `init`, where termbox's own `tb_send` does not check.** Upstream
appends to an output buffer that `init` is about to memset, so the bytes are leaked rather than sent
and nothing reports it — while every neighbouring call answers `TB_ERR_NOT_INIT` for the same
mistake. A caller who reaches this has a bug either way, and one reported at the call beats one that
surfaces as output that never arrived.

## How to bind a vendored C library

This package is the organisation's worked example of the *vendored* half of the story;
[regex](https://github.com/sysl-lang/regex) is the example of binding a library the machine already
has. Three things here are worth taking away.

**1. Put the implementation and the shim in ONE translation unit.** termbox2 is a single header, so
somebody has to write the `.c` that defines `TB_IMPL` and includes it. The shim goes in that same
file, and not beside it, because **a header library's compile-time options are part of its ABI**:
`TB_OPT_ATTR_W` chooses the width of `uintattr_t`, which is the type of `tb_set_cell`'s `fg` and `bg`.
A second `.c` that included the header without setting it identically would declare those functions
with 16-bit parameters, call the 64-bit definitions, and be wrong in a way that compiles, links, and
corrupts colours at run time. One translation unit makes that impossible rather than merely unlikely.

**2. Write a shim for what only C can see, and nothing else.** Three things in `termbox2.h` are
reachable from C and from nothing else (`15 §7`):

| what | why it cannot cross |
|---|---|
| the constants | `TB_RED`, `TB_BOLD`, `TB_KEY_ARROW_UP`, `TB_ERR_NOT_INIT` and a hundred others are `#define`s. A macro has no symbol, so there is nothing for a linker to resolve and nothing for `extern` to name. |
| `struct tb_event` | A layout only the header knows. The shim splits it into eight plain out-parameters, so the struct never crosses. |
| `size_t *out_w` | `tb_print_ex` reports the width it printed through a pointer; pairing that with the return code in the shim is what lets the sysl side answer one `Result`. |

Everything else — `tb_init`, `tb_present`, `tb_set_cell` — is an ordinary symbol with an ordinary
signature, and the binding names it with `extern` directly. **A shim function that only forwarded
would be a second place for the argument order to be wrong.**

**3. Where a table crosses in one direction, write the other direction so it can be tested.** The key
table is a switch from termbox's numbers to an enum's ordinals. A case that is missing, duplicated, or
paired with the wrong constant compiles, links, and quietly reports the wrong key forever — and
nothing can see it, because a key value only arrives from a terminal somebody is typing at. The
inverse switch (`key_code`, which is useful on its own) makes the whole table round-trippable, and
one test walks it.

## Testing

```
sysl test .
```

21 tests, and **none of them needs a terminal** — they set `TERM` themselves so that a build runner
with none does not report itself instead of the binding. What they cover:

- the three compile-time options, each of which is invisible to the linker;
- both directions of the key table, the output-mode table and the input-mode mask;
- the error path across fifteen entry points, plus a failure carrying an `errno` and a code termbox
  has never issued;
- the whole lifecycle against an ordinary file, including that `send`'s bytes come out the far end
  with termbox's own escape sequences around them.

**What they cannot cover is drawing**, and the reason is worth stating rather than hiding. termbox
asks the terminal for its size with an `ioctl`, and a descriptor that is not a terminal has none to
give — so `init` against a file leaves a screen of no width on which every cell is out of bounds.
That is the honest state of a program run under a pipe, and the tests pin exactly it. Covering
`set_cell` and `present` for real would need a pseudo-terminal, which needs `posix_openpt`,
`TIOCSWINSZ` and a `struct winsize` — macros and a layout, so more C, and **C in this package is C
every consumer compiles.** Shipping test scaffolding to every program that draws a menu was judged
not worth it.

So drawing is checked by hand, and here is the recipe, which allocates a pty and gives it a size:

```
sysl build yourprogram.sysl --lib . -o demo
TERM=xterm-256color script -q /dev/null sh -c 'stty rows 24 cols 80; ./demo'
```

The example above was run that way while this was written: 80×24 reported, the borders emitted as
`ESC[36m`, the greeting as `ESC[1m ESC[33m`, `rgb(255, 128, 0)` as `ESC[38;2;255;128;0…` under
`Truecolor`, only changed cells re-sent on each `present`, `x` arriving as `ch = 0x78`, Ctrl-Up as
`key = Up` with `ctrl()` true, and the terminal restored on the way out.

## Upstream

Vendored from [termbox/termbox2](https://github.com/termbox/termbox2) at commit
[`605398f`](https://github.com/termbox/termbox2/commit/605398fa79108412976191e062ea14bd4bd30213)
(8 February 2026), which reports itself as `2.7.0-dev`. It is MIT licensed and copyright 2015–2026
Adam Saponara and 2010–2020 nsf; see `LICENSE`, which carries their notice as redistribution requires.

**A commit rather than the `v2.5.0` release, and the reason is the width tables.** v2.5.0 measures
display width with libc's `wcwidth(3)`, which needs a `setlocale` call the consumer would have to
make and which no sysl program makes; master replaced it with built-in Unicode tables and made libc
the opt-in. That is the difference between a binding that lays CJK out correctly everywhere and one
that does it on the machine the author happened to use. Master has been quiet for months, which is
part of what makes pinning to it reasonable.

Vendoring rather than linking is a deliberate choice and it has a cost worth stating: **upstream
fixes do not arrive on their own.** termbox2 is one file, stable, and low-activity, which is what
makes that an acceptable trade here — it would not be for a library under active development.

## Licence

The binding is ISC; the vendored header is MIT. See [LICENSE](LICENSE).
