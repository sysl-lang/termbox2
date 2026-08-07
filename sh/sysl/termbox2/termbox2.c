/* termbox2's implementation, and the C that a binding to it cannot be written
 * without.
 *
 * ==Why the implementation and the shim are ONE translation unit==
 *
 * termbox2 is a single-header library: the declarations are always visible, and
 * `TB_IMPL` is what asks the header to emit the definitions too. So somebody has
 * to write this file. What is less obvious is why the shim below is in it rather
 * than beside it.
 *
 * **A header library's compile-time options are part of its ABI.** `TB_OPT_ATTR_W`
 * chooses the width of `uintattr_t`, which is the type of the `fg` and `bg`
 * parameters of `tb_set_cell`, `tb_print` and `tb_set_clear_attrs`. A second
 * `.c` file that included `termbox2.h` without setting it identically would
 * declare those functions with `uint16_t` parameters, call the `uint64_t`
 * definitions emitted here, and be wrong in a way that compiles, links, and
 * corrupts colours at run time.
 *
 * One translation unit makes that mismatch impossible rather than merely
 * unlikely, which is worth more than the tidiness of a separate `shim.c`.
 *
 * ==The options, and why these values==
 *
 * `TB_OPT_ATTR_W 64` is the widest setting, and it is what buys `TB_OUTPUT_TRUECOLOR`
 * and the four style attributes above the first eight -- strikeout, double
 * underline, overline and invisible. It costs sixteen bytes per cell against the
 * default of 16 bits. A binding has to choose once for everybody, and a consumer
 * who finds a colour missing has no way to recompile the package; a consumer who
 * finds a terminal buffer larger than they wanted still has a working program.
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
 * ==What the shim is for==
 *
 * Three things in `termbox2.h` are reachable from C and from nothing else
 * (`15 §7` sets out the general form):
 *
 *   - **The constants are `#define`s.** `TB_RED`, `TB_BOLD`, `TB_KEY_ARROW_UP`,
 *     `TB_ERR_NOT_INIT` and the hundred others have no symbols, so there is
 *     nothing for a linker to resolve and nothing for `extern` to name. They are
 *     read from the header here rather than copied into sysl as the numbers one
 *     machine happened to use.
 *   - **`struct tb_event` is a layout only the header knows.** `tb_poll_event`
 *     fills one in storage the caller supplies. The two functions below split it
 *     into eight plain out-parameters, so the struct never crosses.
 *   - **`size_t *out_w`.** `tb_print_ex` reports the width it printed through a
 *     pointer, which is fine, but pairing it with the return code is what lets
 *     the sysl side answer one `Result` rather than two values.
 *
 * Everything else -- `tb_init`, `tb_present`, `tb_set_cell` and the rest -- is an
 * ordinary symbol with an ordinary signature, and the sysl side names it with
 * `extern` directly. A shim function that only forwarded would be a second place
 * for the argument order to be wrong.
 */
#define TB_OPT_ATTR_W 64
#define TB_IMPL
#include "termbox2.h"

/* ===================================================================== *
 * Errors
 * ===================================================================== */

/* The ordinal of `ErrorKind` in `termbox2.sysl`, whose declaration order this
 * list is the agreement with. A code not listed -- a termbox newer than this
 * binding, or a success value somebody wrapped in an `Error` by mistake -- lands
 * on the last ordinal, which is `Other`, so a program keeps running and printing
 * rather than trapping on a number.
 */
enum {
    ORDINAL_ERR = 0,
    ORDINAL_NEED_MORE,
    ORDINAL_INIT_ALREADY,
    ORDINAL_INIT_OPEN,
    ORDINAL_MEM,
    ORDINAL_NO_EVENT,
    ORDINAL_NO_TERM,
    ORDINAL_NOT_INIT,
    ORDINAL_OUT_OF_BOUNDS,
    ORDINAL_READ,
    ORDINAL_RESIZE_IOCTL,
    ORDINAL_RESIZE_PIPE,
    ORDINAL_RESIZE_SIGACTION,
    ORDINAL_POLL,
    ORDINAL_TCGETATTR,
    ORDINAL_TCSETATTR,
    ORDINAL_UNSUPPORTED_TERM,
    ORDINAL_RESIZE_WRITE,
    ORDINAL_RESIZE_POLL,
    ORDINAL_RESIZE_READ,
    ORDINAL_RESIZE_SSCANF,
    ORDINAL_CAP_COLLISION,
    ORDINAL_ERROR_OTHER
};

int sysl_tb_error_ordinal(int code) {
    switch (code) {
        case TB_ERR:                  return ORDINAL_ERR;
        case TB_ERR_NEED_MORE:        return ORDINAL_NEED_MORE;
        case TB_ERR_INIT_ALREADY:     return ORDINAL_INIT_ALREADY;
        case TB_ERR_INIT_OPEN:        return ORDINAL_INIT_OPEN;
        case TB_ERR_MEM:              return ORDINAL_MEM;
        case TB_ERR_NO_EVENT:         return ORDINAL_NO_EVENT;
        case TB_ERR_NO_TERM:          return ORDINAL_NO_TERM;
        case TB_ERR_NOT_INIT:         return ORDINAL_NOT_INIT;
        case TB_ERR_OUT_OF_BOUNDS:    return ORDINAL_OUT_OF_BOUNDS;
        case TB_ERR_READ:             return ORDINAL_READ;
        case TB_ERR_RESIZE_IOCTL:     return ORDINAL_RESIZE_IOCTL;
        case TB_ERR_RESIZE_PIPE:      return ORDINAL_RESIZE_PIPE;
        case TB_ERR_RESIZE_SIGACTION: return ORDINAL_RESIZE_SIGACTION;
        case TB_ERR_POLL:             return ORDINAL_POLL;
        case TB_ERR_TCGETATTR:        return ORDINAL_TCGETATTR;
        case TB_ERR_TCSETATTR:        return ORDINAL_TCSETATTR;
        case TB_ERR_UNSUPPORTED_TERM: return ORDINAL_UNSUPPORTED_TERM;
        case TB_ERR_RESIZE_WRITE:     return ORDINAL_RESIZE_WRITE;
        case TB_ERR_RESIZE_POLL:      return ORDINAL_RESIZE_POLL;
        case TB_ERR_RESIZE_READ:      return ORDINAL_RESIZE_READ;
        case TB_ERR_RESIZE_SSCANF:    return ORDINAL_RESIZE_SSCANF;
        case TB_ERR_CAP_COLLISION:    return ORDINAL_CAP_COLLISION;
        default:                      return ORDINAL_ERROR_OTHER;
    }
}

/* ===================================================================== *
 * Colours and style attributes
 * ===================================================================== */

/* One function per constant rather than one function taking an index. An index
 * would be a second agreement to keep, and the numbers at its call sites would
 * say nothing to a reader; these say what they are.
 *
 * The return type is `uintattr_t`, which the options at the top of this file
 * make `uint64_t`. `tb_attr_width` reports the same width at run time, and the
 * tests check the two agree -- a compile-time option is invisible to the linker,
 * so that assertion is the only thing standing between a mismatch and a silent
 * one.
 */
uintattr_t sysl_tb_default(void)      { return TB_DEFAULT; }
uintattr_t sysl_tb_black(void)        { return TB_BLACK; }
uintattr_t sysl_tb_red(void)          { return TB_RED; }
uintattr_t sysl_tb_green(void)        { return TB_GREEN; }
uintattr_t sysl_tb_yellow(void)       { return TB_YELLOW; }
uintattr_t sysl_tb_blue(void)         { return TB_BLUE; }
uintattr_t sysl_tb_magenta(void)      { return TB_MAGENTA; }
uintattr_t sysl_tb_cyan(void)         { return TB_CYAN; }
uintattr_t sysl_tb_white(void)        { return TB_WHITE; }

uintattr_t sysl_tb_bold(void)         { return TB_BOLD; }
uintattr_t sysl_tb_underline(void)    { return TB_UNDERLINE; }
uintattr_t sysl_tb_reverse(void)      { return TB_REVERSE; }
uintattr_t sysl_tb_italic(void)       { return TB_ITALIC; }
uintattr_t sysl_tb_blink(void)        { return TB_BLINK; }
uintattr_t sysl_tb_hi_black(void)     { return TB_HI_BLACK; }
uintattr_t sysl_tb_bright(void)       { return TB_BRIGHT; }
uintattr_t sysl_tb_dim(void)          { return TB_DIM; }
uintattr_t sysl_tb_strikeout(void)    { return TB_STRIKEOUT; }
uintattr_t sysl_tb_underline_2(void)  { return TB_UNDERLINE_2; }
uintattr_t sysl_tb_overline(void)     { return TB_OVERLINE; }
uintattr_t sysl_tb_invisible(void)    { return TB_INVISIBLE; }

/* ===================================================================== *
 * Modes
 * ===================================================================== */

/* Input mode is a bitmask with a rule termbox states and does not enforce:
 * `TB_INPUT_ESC` and `TB_INPUT_ALT` are alternatives, and asking for both
 * behaves as if only `ESC` had been asked for. The composition and the reading
 * both happen here so that the sysl side can offer two booleans that cannot be
 * combined into something meaningless.
 */
int sysl_tb_input_current(void) { return TB_INPUT_CURRENT; }

int sysl_tb_input_value(int alt, int mouse) {
    return (alt ? TB_INPUT_ALT : TB_INPUT_ESC) | (mouse ? TB_INPUT_MOUSE : 0);
}

int sysl_tb_input_is_alt(int mode) {
    return (mode & TB_INPUT_ALT) != 0 && (mode & TB_INPUT_ESC) == 0;
}

int sysl_tb_input_has_mouse(int mode) { return (mode & TB_INPUT_MOUSE) != 0; }

/* Output mode is an enumeration rather than a mask, so it crosses as the ordinal
 * of `OutputMode` in `termbox2.sysl`. Both directions are needed: the sysl side
 * chooses one to set, and reads back the one that was in effect.
 */
enum {
    ORDINAL_NORMAL = 0,
    ORDINAL_256,
    ORDINAL_216,
    ORDINAL_GRAYSCALE,
    ORDINAL_TRUECOLOR,
    ORDINAL_OUTPUT_OTHER
};

int sysl_tb_output_current(void) { return TB_OUTPUT_CURRENT; }

int sysl_tb_output_value(int ordinal) {
    switch (ordinal) {
        case ORDINAL_NORMAL:    return TB_OUTPUT_NORMAL;
        case ORDINAL_256:       return TB_OUTPUT_256;
        case ORDINAL_216:       return TB_OUTPUT_216;
        case ORDINAL_GRAYSCALE: return TB_OUTPUT_GRAYSCALE;
        case ORDINAL_TRUECOLOR: return TB_OUTPUT_TRUECOLOR;
        /* `UnknownOutput`, or an ordinal from a sysl side newer than this file.
         * `TB_ERR` is not a mode, so `tb_set_output_mode` falls through its own
         * switch and refuses it -- which is what a caller who asked for a mode
         * that does not exist should get, rather than quietly being given
         * `Normal`.
         */
        default:                return TB_ERR;
    }
}

int sysl_tb_output_ordinal(int value) {
    switch (value) {
        case TB_OUTPUT_NORMAL:    return ORDINAL_NORMAL;
        case TB_OUTPUT_256:       return ORDINAL_256;
        case TB_OUTPUT_216:       return ORDINAL_216;
        case TB_OUTPUT_GRAYSCALE: return ORDINAL_GRAYSCALE;
        case TB_OUTPUT_TRUECOLOR: return ORDINAL_TRUECOLOR;
        default:                  return ORDINAL_OUTPUT_OTHER;
    }
}

/* ===================================================================== *
 * Events
 * ===================================================================== */

enum {
    ORDINAL_KEY_EVENT = 0,
    ORDINAL_RESIZE_EVENT,
    ORDINAL_MOUSE_EVENT,
    ORDINAL_EVENT_OTHER
};

int sysl_tb_event_ordinal(int type) {
    switch (type) {
        case TB_EVENT_KEY:    return ORDINAL_KEY_EVENT;
        case TB_EVENT_RESIZE: return ORDINAL_RESIZE_EVENT;
        case TB_EVENT_MOUSE:  return ORDINAL_MOUSE_EVENT;
        default:              return ORDINAL_EVENT_OTHER;
    }
}

uint8_t sysl_tb_mod_alt(void)    { return TB_MOD_ALT; }
uint8_t sysl_tb_mod_ctrl(void)   { return TB_MOD_CTRL; }
uint8_t sysl_tb_mod_shift(void)  { return TB_MOD_SHIFT; }
uint8_t sysl_tb_mod_motion(void) { return TB_MOD_MOTION; }

/* The ordinal of `Key` in `termbox2.sysl`, in its declaration order.
 *
 * **Several termbox key constants share a value**, because the terminal sends
 * one byte for what the reader thinks are two keys: Ctrl-H and Backspace are
 * both 0x08, Ctrl-I and Tab are both 0x09, Ctrl-M and Enter are both 0x0d, and
 * Ctrl-3 is Escape. A switch cannot answer twice, so each value has one
 * canonical name here and it is the one a reader is likelier to have meant.
 * `termbox2.sysl` says so beside the variants that lost.
 */
enum {
    ORDINAL_NUL = 0,
    ORDINAL_CTRL_A, ORDINAL_CTRL_B, ORDINAL_CTRL_C, ORDINAL_CTRL_D,
    ORDINAL_CTRL_E, ORDINAL_CTRL_F, ORDINAL_CTRL_G,
    ORDINAL_BACKSPACE,
    ORDINAL_TAB,
    ORDINAL_CTRL_J, ORDINAL_CTRL_K, ORDINAL_CTRL_L,
    ORDINAL_ENTER,
    ORDINAL_CTRL_N, ORDINAL_CTRL_O, ORDINAL_CTRL_P, ORDINAL_CTRL_Q,
    ORDINAL_CTRL_R, ORDINAL_CTRL_S, ORDINAL_CTRL_T, ORDINAL_CTRL_U,
    ORDINAL_CTRL_V, ORDINAL_CTRL_W, ORDINAL_CTRL_X, ORDINAL_CTRL_Y,
    ORDINAL_CTRL_Z,
    ORDINAL_ESC,
    ORDINAL_CTRL_BACKSLASH,
    ORDINAL_CTRL_RSQ_BRACKET,
    ORDINAL_CTRL_6,
    ORDINAL_CTRL_SLASH,
    ORDINAL_SPACE,
    ORDINAL_BACKSPACE2,
    ORDINAL_F1, ORDINAL_F2, ORDINAL_F3, ORDINAL_F4, ORDINAL_F5, ORDINAL_F6,
    ORDINAL_F7, ORDINAL_F8, ORDINAL_F9, ORDINAL_F10, ORDINAL_F11, ORDINAL_F12,
    ORDINAL_INSERT,
    ORDINAL_DELETE,
    ORDINAL_HOME,
    ORDINAL_END,
    ORDINAL_PAGE_UP,
    ORDINAL_PAGE_DOWN,
    ORDINAL_UP,
    ORDINAL_DOWN,
    ORDINAL_LEFT,
    ORDINAL_RIGHT,
    ORDINAL_BACK_TAB,
    ORDINAL_MOUSE_LEFT,
    ORDINAL_MOUSE_RIGHT,
    ORDINAL_MOUSE_MIDDLE,
    ORDINAL_MOUSE_RELEASE,
    ORDINAL_MOUSE_WHEEL_UP,
    ORDINAL_MOUSE_WHEEL_DOWN,
    ORDINAL_KEY_OTHER
};

/* The inverse: the number termbox reports for a named key.
 *
 * A program mapping a configuration file's "F5" to a key needs it, and it is
 * also what makes the table above testable. `sysl_tb_key_ordinal` and this are
 * two switches over one correspondence, so round-tripping every ordinal through
 * both catches a case that is missing, duplicated, or paired with the wrong
 * constant -- none of which any other test could see, since a key value only
 * arrives from a terminal somebody is typing at.
 *
 * An ordinal that names no key -- `UnknownKey`, or a number from a future
 * version of the sysl side -- answers -1, which is not a value any key has.
 */
int sysl_tb_key_value(int ordinal) {
    switch (ordinal) {
        case ORDINAL_NUL:              return TB_KEY_CTRL_TILDE;
        case ORDINAL_CTRL_A:           return TB_KEY_CTRL_A;
        case ORDINAL_CTRL_B:           return TB_KEY_CTRL_B;
        case ORDINAL_CTRL_C:           return TB_KEY_CTRL_C;
        case ORDINAL_CTRL_D:           return TB_KEY_CTRL_D;
        case ORDINAL_CTRL_E:           return TB_KEY_CTRL_E;
        case ORDINAL_CTRL_F:           return TB_KEY_CTRL_F;
        case ORDINAL_CTRL_G:           return TB_KEY_CTRL_G;
        case ORDINAL_BACKSPACE:        return TB_KEY_BACKSPACE;
        case ORDINAL_TAB:              return TB_KEY_TAB;
        case ORDINAL_CTRL_J:           return TB_KEY_CTRL_J;
        case ORDINAL_CTRL_K:           return TB_KEY_CTRL_K;
        case ORDINAL_CTRL_L:           return TB_KEY_CTRL_L;
        case ORDINAL_ENTER:            return TB_KEY_ENTER;
        case ORDINAL_CTRL_N:           return TB_KEY_CTRL_N;
        case ORDINAL_CTRL_O:           return TB_KEY_CTRL_O;
        case ORDINAL_CTRL_P:           return TB_KEY_CTRL_P;
        case ORDINAL_CTRL_Q:           return TB_KEY_CTRL_Q;
        case ORDINAL_CTRL_R:           return TB_KEY_CTRL_R;
        case ORDINAL_CTRL_S:           return TB_KEY_CTRL_S;
        case ORDINAL_CTRL_T:           return TB_KEY_CTRL_T;
        case ORDINAL_CTRL_U:           return TB_KEY_CTRL_U;
        case ORDINAL_CTRL_V:           return TB_KEY_CTRL_V;
        case ORDINAL_CTRL_W:           return TB_KEY_CTRL_W;
        case ORDINAL_CTRL_X:           return TB_KEY_CTRL_X;
        case ORDINAL_CTRL_Y:           return TB_KEY_CTRL_Y;
        case ORDINAL_CTRL_Z:           return TB_KEY_CTRL_Z;
        case ORDINAL_ESC:              return TB_KEY_ESC;
        case ORDINAL_CTRL_BACKSLASH:   return TB_KEY_CTRL_BACKSLASH;
        case ORDINAL_CTRL_RSQ_BRACKET: return TB_KEY_CTRL_RSQ_BRACKET;
        case ORDINAL_CTRL_6:           return TB_KEY_CTRL_6;
        case ORDINAL_CTRL_SLASH:       return TB_KEY_CTRL_SLASH;
        case ORDINAL_SPACE:            return TB_KEY_SPACE;
        case ORDINAL_BACKSPACE2:       return TB_KEY_BACKSPACE2;
        case ORDINAL_F1:               return TB_KEY_F1;
        case ORDINAL_F2:               return TB_KEY_F2;
        case ORDINAL_F3:               return TB_KEY_F3;
        case ORDINAL_F4:               return TB_KEY_F4;
        case ORDINAL_F5:               return TB_KEY_F5;
        case ORDINAL_F6:               return TB_KEY_F6;
        case ORDINAL_F7:               return TB_KEY_F7;
        case ORDINAL_F8:               return TB_KEY_F8;
        case ORDINAL_F9:               return TB_KEY_F9;
        case ORDINAL_F10:              return TB_KEY_F10;
        case ORDINAL_F11:              return TB_KEY_F11;
        case ORDINAL_F12:              return TB_KEY_F12;
        case ORDINAL_INSERT:           return TB_KEY_INSERT;
        case ORDINAL_DELETE:           return TB_KEY_DELETE;
        case ORDINAL_HOME:             return TB_KEY_HOME;
        case ORDINAL_END:              return TB_KEY_END;
        case ORDINAL_PAGE_UP:          return TB_KEY_PGUP;
        case ORDINAL_PAGE_DOWN:        return TB_KEY_PGDN;
        case ORDINAL_UP:               return TB_KEY_ARROW_UP;
        case ORDINAL_DOWN:             return TB_KEY_ARROW_DOWN;
        case ORDINAL_LEFT:             return TB_KEY_ARROW_LEFT;
        case ORDINAL_RIGHT:            return TB_KEY_ARROW_RIGHT;
        case ORDINAL_BACK_TAB:         return TB_KEY_BACK_TAB;
        case ORDINAL_MOUSE_LEFT:       return TB_KEY_MOUSE_LEFT;
        case ORDINAL_MOUSE_RIGHT:      return TB_KEY_MOUSE_RIGHT;
        case ORDINAL_MOUSE_MIDDLE:     return TB_KEY_MOUSE_MIDDLE;
        case ORDINAL_MOUSE_RELEASE:    return TB_KEY_MOUSE_RELEASE;
        case ORDINAL_MOUSE_WHEEL_UP:   return TB_KEY_MOUSE_WHEEL_UP;
        case ORDINAL_MOUSE_WHEEL_DOWN: return TB_KEY_MOUSE_WHEEL_DOWN;
        default:                       return -1;
    }
}

/* How many ordinals name a key, which is `ORDINAL_KEY_OTHER` -- the one that
 * does not. A test walking the table needs the bound, and reading it from here
 * means the loop cannot drift out of step with the list when a key is added.
 */
int sysl_tb_key_count(void) { return ORDINAL_KEY_OTHER; }

int sysl_tb_key_ordinal(int key) {
    switch (key) {
        case TB_KEY_CTRL_TILDE:       return ORDINAL_NUL;
        case TB_KEY_CTRL_A:           return ORDINAL_CTRL_A;
        case TB_KEY_CTRL_B:           return ORDINAL_CTRL_B;
        case TB_KEY_CTRL_C:           return ORDINAL_CTRL_C;
        case TB_KEY_CTRL_D:           return ORDINAL_CTRL_D;
        case TB_KEY_CTRL_E:           return ORDINAL_CTRL_E;
        case TB_KEY_CTRL_F:           return ORDINAL_CTRL_F;
        case TB_KEY_CTRL_G:           return ORDINAL_CTRL_G;
        case TB_KEY_BACKSPACE:        return ORDINAL_BACKSPACE;
        case TB_KEY_TAB:              return ORDINAL_TAB;
        case TB_KEY_CTRL_J:           return ORDINAL_CTRL_J;
        case TB_KEY_CTRL_K:           return ORDINAL_CTRL_K;
        case TB_KEY_CTRL_L:           return ORDINAL_CTRL_L;
        case TB_KEY_ENTER:            return ORDINAL_ENTER;
        case TB_KEY_CTRL_N:           return ORDINAL_CTRL_N;
        case TB_KEY_CTRL_O:           return ORDINAL_CTRL_O;
        case TB_KEY_CTRL_P:           return ORDINAL_CTRL_P;
        case TB_KEY_CTRL_Q:           return ORDINAL_CTRL_Q;
        case TB_KEY_CTRL_R:           return ORDINAL_CTRL_R;
        case TB_KEY_CTRL_S:           return ORDINAL_CTRL_S;
        case TB_KEY_CTRL_T:           return ORDINAL_CTRL_T;
        case TB_KEY_CTRL_U:           return ORDINAL_CTRL_U;
        case TB_KEY_CTRL_V:           return ORDINAL_CTRL_V;
        case TB_KEY_CTRL_W:           return ORDINAL_CTRL_W;
        case TB_KEY_CTRL_X:           return ORDINAL_CTRL_X;
        case TB_KEY_CTRL_Y:           return ORDINAL_CTRL_Y;
        case TB_KEY_CTRL_Z:           return ORDINAL_CTRL_Z;
        case TB_KEY_ESC:              return ORDINAL_ESC;
        case TB_KEY_CTRL_BACKSLASH:   return ORDINAL_CTRL_BACKSLASH;
        case TB_KEY_CTRL_RSQ_BRACKET: return ORDINAL_CTRL_RSQ_BRACKET;
        case TB_KEY_CTRL_6:           return ORDINAL_CTRL_6;
        case TB_KEY_CTRL_SLASH:       return ORDINAL_CTRL_SLASH;
        case TB_KEY_SPACE:            return ORDINAL_SPACE;
        case TB_KEY_BACKSPACE2:       return ORDINAL_BACKSPACE2;
        case TB_KEY_F1:               return ORDINAL_F1;
        case TB_KEY_F2:               return ORDINAL_F2;
        case TB_KEY_F3:               return ORDINAL_F3;
        case TB_KEY_F4:               return ORDINAL_F4;
        case TB_KEY_F5:               return ORDINAL_F5;
        case TB_KEY_F6:               return ORDINAL_F6;
        case TB_KEY_F7:               return ORDINAL_F7;
        case TB_KEY_F8:               return ORDINAL_F8;
        case TB_KEY_F9:               return ORDINAL_F9;
        case TB_KEY_F10:              return ORDINAL_F10;
        case TB_KEY_F11:              return ORDINAL_F11;
        case TB_KEY_F12:              return ORDINAL_F12;
        case TB_KEY_INSERT:           return ORDINAL_INSERT;
        case TB_KEY_DELETE:           return ORDINAL_DELETE;
        case TB_KEY_HOME:             return ORDINAL_HOME;
        case TB_KEY_END:              return ORDINAL_END;
        case TB_KEY_PGUP:             return ORDINAL_PAGE_UP;
        case TB_KEY_PGDN:             return ORDINAL_PAGE_DOWN;
        case TB_KEY_ARROW_UP:         return ORDINAL_UP;
        case TB_KEY_ARROW_DOWN:       return ORDINAL_DOWN;
        case TB_KEY_ARROW_LEFT:       return ORDINAL_LEFT;
        case TB_KEY_ARROW_RIGHT:      return ORDINAL_RIGHT;
        case TB_KEY_BACK_TAB:         return ORDINAL_BACK_TAB;
        case TB_KEY_MOUSE_LEFT:       return ORDINAL_MOUSE_LEFT;
        case TB_KEY_MOUSE_RIGHT:      return ORDINAL_MOUSE_RIGHT;
        case TB_KEY_MOUSE_MIDDLE:     return ORDINAL_MOUSE_MIDDLE;
        case TB_KEY_MOUSE_RELEASE:    return ORDINAL_MOUSE_RELEASE;
        case TB_KEY_MOUSE_WHEEL_UP:   return ORDINAL_MOUSE_WHEEL_UP;
        case TB_KEY_MOUSE_WHEEL_DOWN: return ORDINAL_MOUSE_WHEEL_DOWN;
        default:                      return ORDINAL_KEY_OTHER;
    }
}

/* `struct tb_event` split into its parts, so that a layout only this header
 * knows never has to cross. The parts are written back only on success, which is
 * what lets the sysl side leave its locals alone on `TB_ERR_NO_EVENT`.
 */
static int split(int rv, struct tb_event *ev, int *type, int *mod, int *key,
    uint32_t *ch, int *w, int *h, int *x, int *y) {
    if (rv != TB_OK) return rv;

    *type = (int)ev->type;
    *mod = (int)ev->mod;
    *key = (int)ev->key;
    *ch = ev->ch;
    *w = (int)ev->w;
    *h = (int)ev->h;
    *x = (int)ev->x;
    *y = (int)ev->y;

    return rv;
}

int sysl_tb_poll_event(int *type, int *mod, int *key, uint32_t *ch, int *w,
    int *h, int *x, int *y) {
    struct tb_event ev;

    return split(tb_poll_event(&ev), &ev, type, mod, key, ch, w, h, x, y);
}

int sysl_tb_peek_event(int timeout_ms, int *type, int *mod, int *key,
    uint32_t *ch, int *w, int *h, int *x, int *y) {
    struct tb_event ev;

    return split(tb_peek_event(&ev, timeout_ms), &ev, type, mod, key, ch, w, h,
        x, y);
}

/* ===================================================================== *
 * Printing and raw output
 * ===================================================================== */

/* `tb_send` with the check every other entry point makes.
 *
 * **termbox's own `tb_send` does not test whether the library is initialized**,
 * and appends to the output buffer regardless. Before `init` that buffer is
 * about to be memset by `tb_reset`, so the bytes are leaked rather than sent and
 * nothing reports it -- while every neighbouring call would have answered
 * `TB_ERR_NOT_INIT` for the same mistake.
 *
 * The binding makes `send` answer like its neighbours. That is a deliberate
 * divergence from upstream and the sysl side says so: a caller who reaches this
 * has a bug either way, and one that is reported at the call beats one that
 * surfaces as a leak.
 */
int sysl_tb_send(const char *buf, size_t nbuf) {
    if_not_init_return();

    return tb_send(buf, nbuf);
}

/* `tb_print_ex` reports the width it printed through a `size_t *`. Pairing that
 * with the return code here is what lets the sysl side answer one `Result`
 * carrying the width, rather than a width and a separate code to check.
 */
int sysl_tb_print(int x, int y, uintattr_t fg, uintattr_t bg, const char *s,
    uint64_t *out_w) {
    size_t w = 0;
    int rv = tb_print_ex(x, y, fg, bg, &w, s);

    *out_w = (uint64_t)w;

    return rv;
}
