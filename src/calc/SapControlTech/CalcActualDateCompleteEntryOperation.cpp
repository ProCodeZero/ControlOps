#include <calc/SapControlTech/CalcActualDateCompleteEntryOperation.hpp>
#include <utils/utilsBoostDate.hpp>
#include <utils/DebugLogger.hpp>

void calcActualInputDates(YearSlices& uniqueSlices, const InitialData& initData)
{
    for (auto& [year, higherTmMap] : uniqueSlices)
    {
        for (auto& [higher_tm, sliceList] : higherTmMap)
        {
            for (auto& slice : sliceList)
            {
                for (auto& frame : slice)
                {
                    logFrameState("CalcActualInputDates_BEFORE", frame);
                    frame.actual_input_dates.clear();

                    KeyCRTYS5 key{
                        frame.culture_id, frame.region_id, frame.t_material_id,
                        frame.year, frame.season
                    };

                    auto it = initData.CRTYS_index_map.find(key);
                    if (it == initData.CRTYS_index_map.end())
                    {
                        logFrameState("CalcActualInputDates_AFTER", frame);
                        continue;
                    }

                    const InitialDataFrame& matchInitData = initData.data[it->second];

                    for (const auto& dep : matchInitData.input_operations)
                    {
                        KeyOrder4 parentKey{
                            matchInitData.culture_id, matchInitData.region_id,
                            dep.operation_order, matchInitData.year
                        };

                        auto parentIt = initData.order_index_map.find(parentKey);
                        if (parentIt == initData.order_index_map.end())
                            continue;

                        const InitialDataFrame& parentInitData = initData.data[parentIt->second];

                        // actual_date of the parent operation, taken from the SAP slices.
                        std::optional<boost::gregorian::date> parentActual;
                        auto& parentFrames = uniqueSlices[year][higher_tm];
                        for (const auto& parentSlice : parentFrames)
                        {
                            for (const auto& candidate : parentSlice)
                            {
                                if (candidate.culture_id == parentInitData.culture_id &&
                                    candidate.region_id == parentInitData.region_id &&
                                    candidate.t_material_id == parentInitData.t_material_id &&
                                    candidate.year == parentInitData.year &&
                                    candidate.season == parentInitData.season)
                                {
                                    parentActual = candidate.actual_date;
                                    break;
                                }
                            }
                            if (parentActual.has_value()) break;
                        }

                        if (parentActual.has_value())
                        {
                            frame.actual_input_dates.push_back(ActualInputDate{
                                parentActual.value() + boost::gregorian::days(dep.deadline),
                                dep.is_alternative });
                        }
                    }

                    logFrameState("CalcActualInputDates_AFTER", frame);
                }
            }
        }
    }
}
