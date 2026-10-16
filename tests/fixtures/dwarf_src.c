// dwarf_src.c - tiny ELF fixture compiled with -g for DWARF line tests.
// Module: test/fixture (C11).
// Owns: a named function and a stable source line for addr_to_line checks.
// Depends: none. Built with -g; not launched under the debugger in pack F.
#include <stdio.h>

__attribute__((noinline)) int antinomia_dwarf_marker(int x) {
    return x + 1; /* line of interest for source view v1 */
}

int main(void) {
    volatile int v = antinomia_dwarf_marker(41);
    (void)v;
    return 0;
}
