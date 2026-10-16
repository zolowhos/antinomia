// loop_inferior.c - tiny sleeper used by Linux debug integration tests.
// Module: test (C11).
// Owns: a long-lived process that exits only on signal.
// Depends: unistd. Built only on Linux for Pack A process-control tests.
#include <unistd.h>

int main(void) {
    for (;;)
        pause();
    return 0;
}
