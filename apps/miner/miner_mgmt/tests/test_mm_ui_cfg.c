// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2026, Canaan Creative Co., Ltd.
 *
 * For the licensing terms, see the LICENSE file (MIT License).
 *
 * NO WARRANTY. The Software is provided "AS IS", without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability, fitness for a
 * particular purpose and non-infringement. In no event shall the authors or copyright
 * holders be liable for any claim, damages or other liability arising from, out of or in
 * connection with the Software or the use or other dealings in the Software.
 */

#include "mm_ui_cfg.h"
#include "mm_cfgstore.h"
#include "ini.h"
#include "osif.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../tests/include/tst_check.h"

static char   log_text[4096]; /* Text of every osif_log() call, one per line */
static size_t log_len;        /* Bytes used in log_text */

void osif_log(osif_loglvl_t lvl, const char *fmt, ...)
{
    va_list ap;

    (void)lvl;
    va_start(ap, fmt);
    vsnprintf(log_text + log_len, sizeof log_text - log_len, fmt, ap);
    va_end(ap);
    log_len += strlen(log_text + log_len);
    if (log_len < sizeof log_text - 1) {
        log_text[log_len++] = '\n';
        log_text[log_len]   = '\0';
    }
}

static void log_clear(void)
{
    log_len     = 0;
    log_text[0] = '\0';
}

static int log_has(const char *needle)
{
    return strstr(log_text, needle) != NULL;
}

static void write_file(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL, "write %s", path);
    if (f) {
        fputs(body, f);
        fclose(f);
    }
}

int main(void)
{
    char path[] = "/tmp/mm_ui_cfg_testXXXXXX";
    int  tfd    = mkstemp(path);
    assert(tfd >= 0);
    close(tfd);
    unlink(path);

    CHECK(mm_cfgstore_open(path) == 0, "store opens over a missing file");
    uint16_t show  = 1;
    int      onoff = 1;
    int pelish = 0;
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 1 && onoff == 1 && pelish == 0, "load missing file keeps defaults");

    CHECK(mm_ui_cfg_save(3, 0, 1) == 0, "save ok");
    CHECK(access(path, F_OK) != 0, "save changes RAM only");
    CHECK(mm_cfgstore_flush() == 1, "flush writes the save");
    CHECK(mm_cfgstore_reload() == 0, "reload the written file");
    show  = 1;
    onoff = 1;
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 3, "show round-trips");
    CHECK(onoff == 0, "onoff round-trips");
    CHECK(pelish == 1, "pelish round-trips");
    ini_t *saved = ini_load(path);
    CHECK(saved && ini_get(saved, "lcd", "id") && ini_get(saved, "lcd", "pelish") &&
                    !ini_get(saved, "lcd", "brightness"),
            "legacy id key is used and brightness is not persisted");
    ini_free(saved);

    CHECK(mm_ui_cfg_save(2, 1, 0) == 0, "re-save ok");
    show   = 0;
    onoff  = 9;
    pelish = 9;
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 2 && onoff == 1 && pelish == 0, "re-save overwrites");
    CHECK(mm_cfgstore_flush() == 1 && mm_cfgstore_reload() == 0, "re-save is written");

    CHECK(mm_ui_cfg_save(show, onoff, pelish) == 0, "unchanged re-save ok");
    CHECK(mm_cfgstore_flush() == 0, "an unchanged re-save writes nothing");
    show  = 0;
    onoff = 9;
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 2 && onoff == 1 && pelish == 0, "non-default show survives boot cycle");

    mm_cfgstore_freeze(1);
    CHECK(mm_ui_cfg_save(4, 0, 1) != 0, "a frozen store refuses the save");
    mm_cfgstore_freeze(0);
    show   = 0;
    onoff  = 9;
    pelish = 9;
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 2 && onoff == 1 && pelish == 0, "a refused save leaves the stored values");

    write_file(path, "[lcd]\nid = 9999\nonoff = 1\nbrightness = 9999\n");
    CHECK(mm_cfgstore_reload() == 0, "reload the garbage file");
    show = 1;
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 1, "invalid positive id keeps the caller's default");
    write_file(path, "[lcd]\nid = -7\n");
    CHECK(mm_cfgstore_reload() == 0, "reload the negative id file");
    log_clear();
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 1, "invalid negative id keeps the caller's default");
    CHECK(log_has("not a valid number"), "a negative id is logged");

    write_file(path, "[lcd]\nid = abc\nonoff = abc\n");
    CHECK(mm_cfgstore_reload() == 0, "reload the non-numeric file");
    show  = 2;
    onoff = 1;
    log_clear();
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 2 && onoff == 1, "non-numeric id and onoff keep the caller's defaults");
    CHECK(log_has("id = \"abc\"") && log_has("onoff = \"abc\""),
            "non-numeric id and onoff are logged");

    write_file(path, "[lcd]\nid = 3x\nonoff = 0x\n");
    CHECK(mm_cfgstore_reload() == 0, "reload the trailing-garbage file");
    show  = 2;
    onoff = 1;
    log_clear();
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(show == 2 && onoff == 1, "trailing garbage keeps the caller's defaults");
    CHECK(log_has("id = \"3x\"") && log_has("onoff = \"0x\""), "trailing garbage is logged");

    write_file(path, "[lcd]\nonoff = 7\n");
    CHECK(mm_cfgstore_reload() == 0, "reload the over-range onoff file");
    onoff = 0;
    log_clear();
    mm_ui_cfg_load(&show, &onoff, &pelish);
    CHECK(onoff == 1, "an onoff above 1 is clamped to 1");
    CHECK(!log_has("not a valid number"), "a clamped onoff is not logged");

    unlink(path);
    char lockpath[sizeof path + 5];
    snprintf(lockpath, sizeof lockpath, "%s.lock", path);
    unlink(lockpath);
    if (fails)
        printf("FAILED (%d)\n", fails);
    else
        printf("PASSED\n");
    return fails ? 1 : 0;
}
