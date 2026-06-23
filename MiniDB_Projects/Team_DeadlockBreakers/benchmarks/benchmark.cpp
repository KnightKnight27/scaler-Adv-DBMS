#include "benchmark_runner.h"
#include "execution/batch.h"

#include <iomanip>
#include <iostream>

using namespace minidb;

int main() {
    Schema schema{{"id", ColumnType::INT}, {"value", ColumnType::INT}};
    std::cout << "MiniDB Track A: batch vs row filter pipeline\n";
    std::cout << "Input preparation is excluded; each result is a 50-run average.\n\n";
    std::cout << std::left << std::setw(9) << "Rows"
              << std::setw(12) << "Batch us" << std::setw(11) << "Row us"
              << std::setw(10) << "Speedup" << std::setw(13) << "Batch CPU"
              << "Row CPU\n";

    for (int size : {10000, 50000, 100000, 200000}) {
        std::vector<Row> data;
        data.reserve(size);
        for (int64_t id = 0; id < size; ++id)
            data.push_back({id, id % 1000});

        auto result = bench::RunFilterBenchmark(data, schema);
        double speedup = static_cast<double>(result.row_wall_us) /
                         result.batch_wall_us;
        std::cout << std::setw(9) << size << std::setw(12) << result.batch_wall_us
                  << std::setw(11) << result.row_wall_us << std::fixed
                  << std::setprecision(2) << std::setw(10) << speedup
                  << std::setw(13) << result.batch_cpu_us << result.row_cpu_us
                  << '\n';
    }

    size_t batch_buffer = BATCH_SIZE * sizeof(Row);
    std::cout << "\nApproximate operator buffer: batch=" << batch_buffer
              << " bytes, row=" << sizeof(Row) << " bytes.\n";
    std::cout << "CPU columns are process CPU microseconds; lower is better.\n";
    return 0;
}
