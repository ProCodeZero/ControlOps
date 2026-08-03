#include <calc/SapControlTech/CalcMinimalPlannedDate.hpp>
#include <utils/utilsBoostDate.hpp>
#include <utils/InputAggregation.hpp>

void calcPlannedDate(InitialData& InitData, int row)
{
    if (InitData.data[row].noinput_deadline.has_value() && InitData.data[row].region_date.has_value())
    {
        InitData.data[row].planned_dates.planned_date =
            InitData.data[row].region_date.value()
            + boost::gregorian::days(InitData.data[row].noinput_deadline.value());
    }
    else
    {
        InitData.data[row].planned_dates.planned_date = std::nullopt;
    }
}

void calcMinimalPlannedDate(InitialData& InitData)
{
    for (int row = 0; row < InitData.Size(); row++)
    {
        calcPlannedDate(InitData, row);

        InitialDataFrame& frame = InitData.data[row];

        // Candidates start with this operation's own region-based planned date.
        std::vector<std::optional<boost::gregorian::date>> candidates;
        candidates.push_back(frame.planned_dates.planned_date);

        // Add the planned date implied by each input dependency.
        bool alternative_contributed = false;
        for (const auto& dep : frame.input_operations)
        {
            KeyOrder4 key{ frame.culture_id, frame.region_id, dep.operation_order, frame.year };
            auto it = InitData.order_index_map.find(key);
            if (it == InitData.order_index_map.end())
                continue;

            const InitialDataFrame& parent = InitData.data[it->second];
            if (parent.planned_dates.minimal_planned_date.has_value())
            {
                candidates.push_back(
                    parent.planned_dates.minimal_planned_date.value()
                    + boost::gregorian::days(dep.deadline));
                if (dep.is_alternative) alternative_contributed = true;
            }
        }

        // Business rule: min without an alternative, max once an alternative
        // dependency produced a date.
        frame.planned_dates.minimal_planned_date =
            aggregateDependencyDates(candidates, alternative_contributed);
    }
}
