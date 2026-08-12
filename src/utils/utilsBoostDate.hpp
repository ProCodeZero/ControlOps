#pragma once
#include <string>
#include <boost/date_time/gregorian/gregorian.hpp>

boost::gregorian::date parse_date(const std::string& date_str);

std::ostream& operator<<(std::ostream& os, const boost::gregorian::date& d);

// Date aggregation now lives in utils/InputAggregation.hpp
// (aggregateMax / aggregateMin / aggregateDependencyDates) — it works on a
// list of dependencies instead of a fixed argument pack.

