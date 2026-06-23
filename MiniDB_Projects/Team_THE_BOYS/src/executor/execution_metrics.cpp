#include "executor/execution_metrics.h"

namespace minidb {

ExecutionMetrics ExecutionMetricsHolder::metrics_{};

void ExecutionMetricsHolder::Reset() { metrics_ = {}; }

ExecutionMetrics& ExecutionMetricsHolder::Get() { return metrics_; }

}  // namespace minidb
