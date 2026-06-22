// Track A benchmark: the same analytical query on the row-oriented executor
// versus the columnar / vectorised path. Prints timings and the speedup.
#include <chrono>
#include <cstdio>
#include <string>

#include "../src/minidb/columnar/column_store.h"
#include "../src/minidb/columnar/vectorized.h"
#include "../src/minidb/db.h"

using namespace minidb;
using Clock = std::chrono::high_resolution_clock;

static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main(int argc, char** argv) {
    int rows = argc > 1 ? std::atoi(argv[1]) : 200000;
    std::remove("bench.db.data");
    std::remove("bench.db.wal");
    std::remove("bench.db.meta");

    std::printf("MiniDB Track A benchmark  (%d rows)\n", rows);
    std::printf("query: total of `amount` over rows where region < 5\n\n");

    Schema schema = {{"id", Type::Int}, {"region", Type::Int}, {"amount", Type::Int}};
    ColumnStore cstore(schema);

    Database db("bench.db");
    db.execute("CREATE TABLE sales (id INT, region INT, amount INT)");
    db.execute("BEGIN");
    for (int i = 0; i < rows; ++i) {
        int region = i % 10;
        int amount = (i * 7 + 3) % 1000;
        db.execute("INSERT INTO sales VALUES (" + std::to_string(i) + ", " +
                   std::to_string(region) + ", " + std::to_string(amount) + ")");
        Tuple t = {Value::make_int(i), Value::make_int(region), Value::make_int(amount)};
        cstore.append(t);
    }
    db.execute("COMMIT");

    // Row-oriented path: full tuple decode + iterator pipeline. The engine
    // returns the matching `amount` values; the harness sums them.
    auto t0 = Clock::now();
    Result row = db.execute("SELECT amount FROM sales WHERE region < 5");
    int64_t row_sum = 0;
    for (const auto& r : row.rows) row_sum += std::atoll(r[0].c_str());
    auto t1 = Clock::now();
    std::string row_answer = std::to_string(row_sum);

    // Columnar / vectorised path: scan only the two needed columns in batches.
    int col_amount = cstore.column("amount");
    int col_region = cstore.column("region");
    auto t2 = Clock::now();
    int64_t col_answer =
        vectorized_sum(cstore.ints(col_amount), cstore.ints(col_region), "<", 5, true);
    auto t3 = Clock::now();

    double row_ms = ms(t0, t1);
    double col_ms = ms(t2, t3);

    std::printf("row engine (heap scan + filter + sum) : %8.3f ms   -> total = %s\n", row_ms,
                row_answer.c_str());
    std::printf("columnar + vectorised (batch=%d)     : %8.3f ms   -> total = %lld\n", VECTOR_BATCH,
                col_ms, static_cast<long long>(col_answer));
    std::printf("\nspeedup: %.1fx   (results match: %s)\n", col_ms > 0 ? row_ms / col_ms : 0.0,
                row_answer == std::to_string(col_answer) ? "yes" : "NO");
    return 0;
}
