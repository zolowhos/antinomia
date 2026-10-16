// loop_inferior.c - tiny sleeper used by Linux debug integration tests.
// Module: test (C11).
// Owns: a long-lived process that exits only on signal, plus one export.
// Depends: unistd. Built only on Linux for Pack A/F process-control tests.
#include <unistd.h>

#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
void antinomia_marker(void) {
}

int main(void) {
    antinomia_marker();
    for (;;)
        pause();
    return 0;
}
