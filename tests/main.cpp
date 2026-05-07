#include "harness.h"
#include <signal.h>

int main() {
    signal(SIGPIPE, SIG_IGN);  // StdioTransport writes to closed pipes; treat as EPIPE not crash
    return run_tests();
}
