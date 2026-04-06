#pragma once

/* Test harness: Unix domain socket server for autonomous debugging.
 * Socket path: /tmp/mgs_test.sock
 * Protocol: newline-delimited JSON (one command per line, one response per line).
 * TEST_HARNESS_tick() must be called once per frame from the main loop. */

void TEST_HARNESS_init(void);
void TEST_HARNESS_tick(void);
