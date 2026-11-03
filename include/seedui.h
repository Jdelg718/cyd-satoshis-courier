#pragma once
#include "game.h"
// Takes over the display after a run: decides eligibility, generates the
// mnemonic, shows it, and offers the guarded SD write. Returns to the caller
// when the player is done.
void seedui_run(const run_result_t *r);

#ifdef SEED_WRITE_TEST
// Exercises the real seed-file writer end to end against the SD card with a
// known mnemonic, reads it back, and removes it. Build with -DSEED_WRITE_TEST.
bool seedui_write_test(void);
#endif
