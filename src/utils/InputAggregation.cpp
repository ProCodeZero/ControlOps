#include <utils/InputAggregation.hpp>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<int> splitInts(const std::string& csv) {
    std::vector<int> out;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        std::string t = trim(token);
        if (t.empty()) continue;   
        size_t pos = 0;
        int value = std::stoi(t, &pos);   
        if (pos != t.size())
            throw std::invalid_argument("Non-integer token in list: '" + t + "'");
        out.push_back(value);
    }
    return out;
}

} // namespace

std::vector<InputOperation>
parseInputOperations(const std::string& orders, const std::string& deadlines) {
    std::vector<int> ord = splitInts(orders);
    std::vector<int> dl  = splitInts(deadlines);
    if (ord.size() != dl.size())
        throw std::runtime_error(
            "input_operation_order/input_deadline count mismatch: "
            + std::to_string(ord.size()) + " vs " + std::to_string(dl.size()));

    std::vector<InputOperation> result;
    result.reserve(ord.size());
    for (size_t i = 0; i < ord.size(); ++i)
        result.push_back(InputOperation{ ord[i], dl[i], i != 0 });
    return result;
}
