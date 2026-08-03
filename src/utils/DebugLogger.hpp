#pragma once

#include <data_struct/SapData.hpp>
#include <spdlog/spdlog.h>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <string>

inline std::string dateToStr(const std::optional<boost::gregorian::date>& d) {
    if (d) return boost::gregorian::to_simple_string(*d);
    return "NULL";
}

inline void logFrameState(const std::string& step, const SapDataFrame& frame) {
    // Фильтруем только нужный higher_tm и год
    if (frame.higher_tm == "TM-04-41-35-0008" && frame.year == 2026) {
        std::string actual_inputs;
        for (const auto& d : frame.actual_input_dates)
            actual_inputs += boost::gregorian::to_simple_string(d.date)
                           + (d.is_alternative ? "(alt) " : " ");

        spdlog::info(
            "[DEBUG {}] higher_tm={}, year={}, t_material_id={}, order={}, "
            "actual_date={}, start_date={}, is_completed={}, is_started={}, "
            "actual_input_dates=[{}], sawing_date={}, "
            "resawing_date={}, minimal_date={}, status={}",
            step,
            frame.higher_tm,
            frame.year,
            frame.t_material_id,
            frame.order.value_or(-1),
            dateToStr(frame.actual_date),
            dateToStr(frame.start_date),
            frame.is_completed,
            frame.is_started,
            actual_inputs,
            dateToStr(frame.sawing_date),
            dateToStr(frame.resawing_date),
            dateToStr(frame.minimal_date),
            frame.status.value_or("NULL")
        );
    }
}
