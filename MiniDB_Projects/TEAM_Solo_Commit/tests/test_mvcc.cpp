// Track B: MVCC snapshot visibility. Readers see the version current as of their snapshot;
// uncommitted and aborted versions are never visible; deletes hide the row going forward.
#include "mvcc/version_store.h"
#include "test_util.h"

using namespace minidb;

static std::string Read(VersionStore& s, int64_t ts, int64_t key) {
    std::string o;
    return s.ReadSnapshot(ts, key, &o) ? o : "(none)";
}

int main() {
    VersionStore s;
    s.Write(1, 1, "v10"); s.Commit(1, 10);
    s.Write(2, 1, "v20"); s.Commit(2, 20);

    CHECK(Read(s, 15, 1) == "v10");   // snapshot between the two commits
    CHECK(Read(s, 25, 1) == "v20");   // after the second commit
    CHECK(Read(s, 5, 1) == "(none)"); // before any commit

    s.Write(3, 1, "pending");         // uncommitted writer
    CHECK(Read(s, 99, 1) == "v20");   // invisible to readers
    s.Abort(3);
    CHECK(Read(s, 99, 1) == "v20");   // still invisible after abort

    s.Write(4, 1, "", /*deleted=*/true); s.Commit(4, 30);
    CHECK(Read(s, 35, 1) == "(none)"); // deleted as of snapshot 35
    CHECK(Read(s, 25, 1) == "v20");    // time-travel: still visible at 25

    CHECK(s.VersionCount(1) == 4);
    return minidb_test::Done("mvcc");
}
