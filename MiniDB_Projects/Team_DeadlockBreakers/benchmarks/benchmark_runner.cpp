#include "benchmark_runner.h"
#include "benchmark_sources.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <stdexcept>

namespace minidb::bench {

struct Timing {
    long long wall_us;
    long long cpu_us;
};

class Timer {
public:
    Timer() : wall_(std::chrono::steady_clock::now()), cpu_(std::clock()) {}
    Timing Stop() const {
        auto wall = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall_).count();
        auto cpu = static_cast<long long>(
            (std::clock() - cpu_) * 1000000.0 / CLOCKS_PER_SEC);
        return {wall, cpu};
    }
private:
    std::chrono::steady_clock::time_point wall_;
    std::clock_t cpu_;
};

static WhereClause FirstFilter() {
    return {{{"value", CmpOp::GTE, int64_t{0}}}, LogicOp::AND};
}

static WhereClause SecondFilter() {
    return {{{"value", CmpOp::LT, int64_t{1000}}}, LogicOp::AND};
}

static WhereClause ThirdFilter() {
    return {{{"id", CmpOp::GTE, int64_t{0}}}, LogicOp::AND};
}

Result RunFilterBenchmark(const std::vector<Row>& data, const Schema& schema) {
    constexpr int runs = 50;
    Result result;
    result.rows = data.size();
    size_t row_matches = 0;

    for (int run = 0; run < runs; ++run) {
        auto batch_source = std::make_unique<BatchSource>(data, schema);
        auto batch_filter = std::make_unique<FilterOperator>(
            std::move(batch_source), FirstFilter(), schema);
        auto second_batch_filter = std::make_unique<FilterOperator>(
            std::move(batch_filter), SecondFilter(), schema);
        FilterOperator batch_pipeline(
            std::move(second_batch_filter), ThirdFilter(), schema);

        Timer batch_timer;
        batch_pipeline.Open();
        result.matches = 0;
        for (;;) {
            TupleBatch output = batch_pipeline.Next();
            if (output.IsEmpty()) break;
            result.matches += output.Size();
        }
        batch_pipeline.Close();
        Timing batch_time = batch_timer.Stop();
        result.batch_wall_us += batch_time.wall_us;
        result.batch_cpu_us += batch_time.cpu_us;

        auto row_source = std::make_unique<RowSource>(data, schema);
        auto row_filter = std::make_unique<FilterRowOperator>(
            std::move(row_source), FirstFilter(), schema);
        auto second_row_filter = std::make_unique<FilterRowOperator>(
            std::move(row_filter), SecondFilter(), schema);
        FilterRowOperator row_pipeline(
            std::move(second_row_filter), ThirdFilter(), schema);

        Timer row_timer;
        row_pipeline.Open();
        Row output;
        row_matches = 0;
        while (row_pipeline.Next(output)) row_matches++;
        row_pipeline.Close();
        Timing row_time = row_timer.Stop();
        result.row_wall_us += row_time.wall_us;
        result.row_cpu_us += row_time.cpu_us;
    }

    if (result.matches != row_matches)
        throw std::runtime_error("batch and row results differ");
    result.batch_wall_us /= runs;
    result.row_wall_us /= runs;
    result.batch_cpu_us /= runs;
    result.row_cpu_us /= runs;
    return result;
}

} // namespace minidb::bench
