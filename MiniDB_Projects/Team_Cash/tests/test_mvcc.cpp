// Tests for MVCC: snapshot visibility and version chains.
#include "check.h"
#include "mvcc.h"

using namespace minidb;

int main() {
    MVCCStore mv;
    std::string out;

    mv.put(1, "v1", 1);
    mv.commit(1);
    CHECK(mv.read(1, 5, out) && out == "v1");     // committed version is visible

    mv.put(1, "v2", 10);                           // a writer's new, uncommitted version
    CHECK(mv.read(1, 5, out) && out == "v1");      // reader does not block, still sees v1
    CHECK(mv.read(1, 100, out) && out == "v1");    // v2 not committed: invisible to everyone

    mv.commit(10);
    CHECK(mv.read(1, 100, out) && out == "v2");    // new snapshot now sees v2
    CHECK(mv.read(1, 5, out) && out == "v1");      // old snapshot still sees v1
    CHECK(mv.versionCount(1) == 2);

    CHECK(!mv.read(2, 100, out));                  // unknown key

    REPORT();
}
