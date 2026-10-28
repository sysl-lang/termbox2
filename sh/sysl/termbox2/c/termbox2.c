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
 * Bracketed paste is here because it lives inside termbox's parser: the hook
 * termbox calls reads and consumes termbox's own input buffer, which is static
 * to this translation unit, and `sysl_tb_shutdown` has to add to the output
 * buffer termbox flushes on the way out.
 *
 * Everything else -- `tb_init`, `tb_present`, `tb_set_cell` and the rest -- is an
 * ordinary symbol with an ordinary signature, and `c.sysl` names it with `extern`
 * directly. A shim function that only forwarded would be a second place for the
 * argument order to be wrong.
 */
#define TB_IMPL
#include "options.h"

/* ===================================================================== *
 * Bracketed paste
 * ===================================================================== */

/* A terminal with bracketed paste on (`ESC[?2004h`) wraps whatever the reader
 * pastes in `ESC[200~` ... `ESC[201~`. termbox knows neither marker, so without
 * this the opening one arrives as Escape followed by `[200~` typed, and the
 * text as keystrokes.
 *
 * **The whole paste becomes one event.** termbox offers exactly one way into
 * its parser: the `TB_FUNC_EXTRACT_PRE` hook, asked first whenever the input
 * buffer starts with an Escape. `paste_extract` answers it:
 *
 *   - the buffer is a strict prefix of the opening marker: `TB_ERR_NEED_MORE`,
 *     so termbox reads more rather than reporting Escape;
 *   - it opens a paste whose closing marker has not arrived: `TB_ERR_NEED_MORE`
 *     again, and the bytes stay in termbox's input buffer, which grows by
 *     doubling;
 *   - the closing marker is there: the text between the markers is copied out,
 *     the event's type is `SYSL_TB_EVENT_PASTE`, and both markers and the text
 *     are consumed.
 *
 * **The search for the closing marker resumes where the last one stopped**
 * (`paste.scanned`), so a paste arriving in many reads is searched once in
 * total rather than once per read. Nothing between the markers is interpreted:
 * an Escape, a newline or an arrow key's sequence inside a paste is text.
 */
static const char PASTE_BEGIN[] = "\x1b[200~";
static const char PASTE_END[] = "\x1b[201~";
#define PASTE_MARK ((size_t)6)

static struct {
    int enabled;
    /* Every offset below this, past the opening marker, is known not to start
     * the closing one. */
    size_t scanned;
    char *text;
    size_t len;
    size_t cap;
} paste;

/* How much of the opening marker the input buffer starts with: 0 when it does
 * not start like one at all. */
static size_t paste_opened(void) {
    struct bytebuf *in = &global.in;
    size_t n = in->len < PASTE_MARK ? in->len : PASTE_MARK;

    if (n == 0 || memcmp(in->buf, PASTE_BEGIN, n) != 0) return 0;

    return n;
}

static int paste_keep(const char *s, size_t n) {
    if (n > paste.cap) {
        char *grown = (char *)tb_realloc(paste.text, n);

        if (!grown) return TB_ERR_MEM;
        paste.text = grown;
        paste.cap = n;
    }
    if (n > 0) memcpy(paste.text, s, n);
    paste.len = n;

    return TB_OK;
}

static int paste_extract(struct tb_event *event, size_t *consumed) {
    struct bytebuf *in = &global.in;
    size_t opened = paste_opened();

    if (!paste.enabled || opened == 0) {
        paste.scanned = 0;
        return TB_ERR;
    }
    if (opened < PASTE_MARK) return TB_ERR_NEED_MORE;

    size_t at = paste.scanned > PASTE_MARK ? paste.scanned : PASTE_MARK;

    if (at > in->len) at = PASTE_MARK;

    while (at + PASTE_MARK <= in->len) {
        const char *esc = memchr(in->buf + at, '\x1b', in->len - at);

        if (!esc) {
            at = in->len;
            break;
        }
        at = (size_t)(esc - in->buf);
        if (at + PASTE_MARK > in->len) break;
        if (memcmp(esc, PASTE_END, PASTE_MARK) == 0) {
            int rv = paste_keep(in->buf + PASTE_MARK, at - PASTE_MARK);

            if (rv != TB_OK) return rv;
            event->type = SYSL_TB_EVENT_PASTE;
            event->mod = 0;
            event->key = 0;
            event->ch = 0;
            *consumed = at + PASTE_MARK;
            paste.scanned = 0;
            return TB_OK;
        }
        at++;
    }

    paste.scanned = at;
    return TB_ERR_NEED_MORE;
}

/* Whether the input buffer holds the start of a paste whose end has not come. */
static int paste_pending(void) {
    return paste.enabled && paste_opened() > 0;
}

/* **A lone Escape at the end of a read.** In `TB_INPUT_ESC` termbox reports an
 * Escape the moment the buffer holds nothing else, without asking the hook, so
 * a paste whose opening marker straddles two reads would lose its first byte.
 * The terminal wrote the marker in one piece, so the rest is already waiting:
 * one read that does not wait collects it before termbox decides.
 */
static void paste_finish_marker(void) {
    struct bytebuf *in = &global.in;

    if (!paste.enabled || in->len != 1 || in->buf[0] != '\x1b') return;

    fd_set fds;
    struct timeval tv = {0, 0};
    char buf[TB_OPT_READ_BUF];

    FD_ZERO(&fds);
    FD_SET(global.rfd, &fds);
    if (select(global.rfd + 1, &fds, NULL, NULL, &tv) <= 0) return;

    ssize_t n = read(global.rfd, buf, sizeof(buf));

    if (n > 0) bytebuf_nputs(in, buf, (size_t)n);
}

static void paste_reset(void) {
    paste.enabled = 0;
    paste.scanned = 0;
}

static int paste_mode(int on) {
    int rv;

    if_not_init_return();

    if_err_return(rv, bytebuf_puts(&global.out, on ? "\x1b[?2004h" : "\x1b[?2004l"));
    if_err_return(rv, bytebuf_flush(&global.out, global.wfd));
    if_err_return(rv, tb_set_func(TB_FUNC_EXTRACT_PRE, on ? paste_extract : NULL));

    paste.enabled = on;
    paste.scanned = 0;

    return TB_OK;
}

int sysl_tb_enable_bracketed_paste(void) {
    return paste_mode(1);
}

int sysl_tb_disable_bracketed_paste(void) {
    return paste_mode(0);
}

/* The text of the last `SYSL_TB_EVENT_PASTE`, valid until the next one. */
const char *sysl_tb_paste_text(size_t *n) {
    *n = paste.len;

    return paste.text ? paste.text : "";
}

/* `tb_shutdown`, turning bracketed paste back off first: a terminal left with
 * it on brackets every later paste into the shell. termbox's own shutdown
 * flushes its output buffer, so the sequence rides along with the rest of the
 * restore. */
int sysl_tb_shutdown(void) {
    if (global.initialized && paste.enabled)
        bytebuf_puts(&global.out, "\x1b[?2004l");

    paste_reset();
    tb_free(paste.text);
    paste.text = NULL;
    paste.len = 0;
    paste.cap = 0;

    return tb_shutdown();
}

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

    paste_finish_marker();

    return split(tb_poll_event(&ev), &ev, type, mod, key, ch, w, h, x, y);
}

/* **A paste still arriving is not an answer.** termbox's own peek reads once
 * and reports `TB_ERR_NEED_MORE` when the bytes so far stop inside a sequence;
 * while that sequence is a paste, the peek keeps reading until the paste ends
 * or the terminal goes quiet for the timeout, which is `TB_ERR_NO_EVENT` like
 * any other quiet terminal. What has arrived stays buffered for the next call.
 */
int sysl_tb_peek_event(int timeout_ms, int *type, int *mod, int *key,
    uint32_t *ch, int *w, int *h, int *x, int *y) {
    struct tb_event ev;
    int rv;

    paste_finish_marker();

    do {
        rv = tb_peek_event(&ev, timeout_ms);
    } while (rv == TB_ERR_NEED_MORE && paste_pending());

    return split(rv, &ev, type, mod, key, ch, w, h, x, y);
}

/* ===================================================================== *
 * Reading a cell back
 * ===================================================================== */

/* The codepoints of one cell, as many as fit in `out`, with the true count in
 * `*out_n`. **`struct tb_cell` is a layout only this header knows**, and with
 * `TB_OPT_EGC` a cell holds either one codepoint (`ch`, `nech == 0`) or a
 * cluster (`ech`, `nech` of them), so the sysl side is handed the codepoints
 * rather than the struct. A caller whose buffer was too small sees
 * `*out_n > cap` and knows how much more to ask for.
 */
int sysl_tb_cell_codepoints(int x, int y, int back, uint32_t *out, size_t cap,
    size_t *out_n) {
    struct tb_cell *cell;
    int rv = tb_get_cell(x, y, back, &cell);

    if (rv != TB_OK) return rv;

    size_t n = cell->nech > 0 ? cell->nech : 1;

    for (size_t i = 0; i < n && i < cap; i++)
        out[i] = cell->nech > 0 ? cell->ech[i] : cell->ch;

    *out_n = n;

    return TB_OK;
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
