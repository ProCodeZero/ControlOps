#pragma once
#include <string>
#include <vector>
#include <optional>

struct InputOperation {
    int  operation_order;
    int  deadline;
    bool is_alternative;   // false for index 0, true for the rest
};

std::vector<InputOperation>
parseInputOperations(const std::string& orders, const std::string& deadlines);

template <typename T>
std::optional<T> aggregateMax(const std::vector<std::optional<T>>& xs) {
    std::optional<T> best;
    for (const auto& x : xs) {
        if (!x) continue;
        if (!best || *best < *x) best = x;
    }
    return best;
}
template <typename T>
std::optional<T> aggregateMin(const std::vector<std::optional<T>>& xs) {
    std::optional<T> best;
    for (const auto& x : xs) {
        if (!x) continue;
        if (!best || *x < *best) best = x;
    }
    return best;
}
template <typename T>
std::optional<T> aggregateDependencyDates(const std::vector<std::optional<T>>& candidates,
                                          bool alternative_contributed) {
    return alternative_contributed ? aggregateMax(candidates) : aggregateMin(candidates);
}
