/* termbox2's implementation, and the three things a binding to it cannot reach
 * from sysl.
 *
 * ==What is here, and what used to be==
 *
 * termbox2 is a single-header library: the declarations are always visible, and
 * `TB_IMPL` is what asks the header to emit the definitions too. So somebody has
 * to write this file. **The compile-time options are NOT written here** -- they
 * are in `options.h`, which this file includes, because the `c const` block in
 * `c.sysl` is measured from a second translation unit that has to see the same
 * ones. That file says what the options are and why.
 *
 * This file was three times its present length until `c const` arrived. It carried
 * a function per `#define` -- twenty-one colours and attributes, four modifier
 * masks, the input-mode bits -- and four hand-written translation tables pairing
 * termbox's constants with the declaration order of an enum in `termbox2.sysl`,
 * with nothing comparing the two halves. `c.sysl` asks the C compiler for those
 * numbers directly and matches on them in sysl, so both languages now read the
 * same names out of the same header and the agreement is checked by the compiler
 * rather than maintained by hand.
 *
 * ==What genuinely cannot leave==
 *
 * Two of `15 §7`'s three C-only shapes are left, and both are about a *layout*
 * rather than a value:
 *
 *   - **`struct tb_event` is a layout only the header knows.** `tb_poll_event`
 *     fills one in storage the caller supplies. The two functions below split it
 *     into eight plain out-parameters, so the struct never crosses.
 *   - **`size_t *out_w`.** `tb_print_ex` reports the width it printed through a
 *     pointer, which is fine, but pairing it with the return code is what lets
 *     the sysl side answer one `Result` rather than two values.
 *
 * `sysl_tb_send` is here for a third reason, which is a deliberate divergence
 * from upstream rather than a limit of the language; the comment above it says so.
 *
 * Everything else -- `tb_init`, `tb_present`, `tb_set_cell` and the rest -- is an
 * ordinary symbol with an ordinary signature, and `c.sysl` names it with `extern`
 * directly. A shim function that only forwarded would be a second place for the
 * argument order to be wrong.
 */
#define TB_IMPL
#include "options.h"

/* ===================================================================== *
 * Events
 * ===================================================================== */

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
 *
 * `if_not_init_return` is a macro of termbox's own, which is the other reason
 * this cannot be written in sysl: nothing in the API answers whether the library
 * is initialized.
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
