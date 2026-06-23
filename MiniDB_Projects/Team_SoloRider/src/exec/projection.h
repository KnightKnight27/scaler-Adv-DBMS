#pragma once
#include "exec/operator.h"
#include <vector>

namespace minidb {

class Projection : public Operator {
public:
    Projection(std::unique_ptr<Operator> child, const std::vector<int>& column_indices);
    ~Projection() override = default;

    void Open() override;
    bool Next(Tuple& out) override;
    void Close() override;
    const Schema& get_schema() const override { return output_schema_; }

private:
    std::unique_ptr<Operator> child_;
    std::vector<int> column_indices_;
    Schema output_schema_;
};

} // namespace minidb
