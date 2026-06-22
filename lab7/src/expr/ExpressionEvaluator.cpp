#include "expr/ExpressionEvaluator.h"

#include <utility>

#include "expr/RpnEvaluator.h"
#include "expr/ShuntingYard.h"
#include "expr/Tokenizer.h"

namespace expr {

ExpressionEvaluator::ExpressionEvaluator()
    : tokenizer_(std::make_unique<Tokenizer>()),
      converter_(std::make_unique<ShuntingYard>()),
      evaluator_(std::make_unique<RpnEvaluator>()) {}

ExpressionEvaluator::ExpressionEvaluator(std::unique_ptr<ITokenizer> tokenizer,
                                         std::unique_ptr<IInfixToPostfix> converter,
                                         std::unique_ptr<IRpnEvaluator> evaluator)
    : tokenizer_(std::move(tokenizer)),
      converter_(std::move(converter)),
      evaluator_(std::move(evaluator)) {}

double ExpressionEvaluator::evaluate(const std::string& expression) const {
    return evaluator_->evaluate(converter_->toPostfix(tokenizer_->tokenize(expression)));
}

std::vector<Token> ExpressionEvaluator::toPostfix(const std::string& expression) const {
    return converter_->toPostfix(tokenizer_->tokenize(expression));
}

}  // namespace expr
