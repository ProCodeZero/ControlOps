#include <data_struct/SapData.hpp>
#include <utils/utilsBoostDate.hpp>
#include <utils/utilsOptional.hpp>
#include <unordered_set>


int SapData::Size()
{
    return data.size();
}

void SapData::Print()
{
    for (const auto& row : data)
    {
        std::cout << "culture_id: " << row.culture_id << std::endl;
        std::cout << "region_id: " << row.region_id << std::endl;
        std::cout << "t_material_id: " << row.t_material_id << std::endl;
        std::cout << "pu_id: " << row.pu_id << std::endl;
        std::cout << "higher_tm: " << row.higher_tm << std::endl;
        std::cout << "season: " << row.season << std::endl;
        std::cout << "calendar_day: " << row.calendar_day << std::endl;
        std::cout << "planned_volume: " << row.planned_volume << std::endl;
        std::cout << "actual_volume: " << row.actual_volume << std::endl;
        std::cout << "year: " << row.year << std::endl;

        std::cout << "\n\n";
    }
}

SapData::SapData(const pqxx::result& rows)
{
    auto optInt = [](const pqxx::row& r, const char* col) -> std::optional<int>
        {
            return r[col].is_null() ? std::nullopt : std::make_optional(r[col].as<int>());
        };

    for (const auto& row : rows)
    {
        SapDataFrame frame;
        frame.culture_id = row["culture_id"].as<int>();
        frame.region_id = row["region_id"].as<int>();
        frame.t_material_id = row["t_material_id"].as<int>();
        frame.pu_id = row["pu_id"].as<int>();
        frame.higher_tm = row["higher_tm"].as<std::string>();
        frame.season = row["season"].as<std::string>();
        frame.calendar_day = boost::gregorian::from_simple_string(row["calendar_day"].as<std::string>());
        frame.planned_volume = row["planned_volume"].as<int>();
        frame.actual_volume = row["actual_volume"].as<int>();
        frame.year = row["year"].as<int>();

        data.push_back(std::move(frame));
    }
}

IDSReseeding::IDSReseeding(const pqxx::result& rows)
{
    for (const auto& row : rows)
    {
        IDs.push_back(std::move(row["id"].as<int>()));
    }
}

int IDSReseeding::Size()
{
    return IDs.size();
}

void IDSReseeding::Print()
{
    for (const auto& row : IDs)
    {
        std::cout << "id: " << row << std::endl;
    }
}

IDSSeeding::IDSSeeding(const pqxx::result& rows)
{
    for (const auto& row : rows)
    {
        IDs.push_back(std::move(row["id"].as<int>()));
    }
}

int IDSSeeding::Size()
{
    return IDs.size();
}

void IDSSeeding::Print()
{
    for (const auto& row : IDs)
    {
        std::cout << "id: " << row << std::endl;
    }
}

// Разделение данных по year -> higher_tm -> куски по правилу t_material_id = 421 и planned_volume != 0
YearSlices sliceSapData(const std::vector<SapDataFrame>& data, const IDSReseeding& reseeding)
{
    YearSlices result;

    for (const auto& frame : data)
    {
        auto& slices = result[frame.year][frame.higher_tm];

        if (slices.empty())
            slices.emplace_back(); // начинаем первый срез

        auto& current_slice = slices.back();

        // проверка наличия t_material_id в списке reseeding.IDs
        bool is_reseeding_id = std::find(reseeding.IDs.begin(), reseeding.IDs.end(), frame.t_material_id) != reseeding.IDs.end();

        if (is_reseeding_id && frame.planned_volume != 0)
        {
            slices.emplace_back();
            slices.back().push_back(frame);
        }
        else
        {
            current_slice.push_back(frame);
        }
    }

    return result;
}

// Метод формирует структуру YearSlices с уникальными t_material_id внутри каждого среза
YearSlices makeUniqueTMaterialSlices(const YearSlices& originalSlices)
{
    YearSlices uniqueSlices;

    for (const auto& [year, higherTmMap] : originalSlices)
    {
        for (const auto& [higher_tm, slices] : higherTmMap)
        {
            MaterialSlices& uniqueMaterialSlices = uniqueSlices[year][higher_tm];

            for (const auto& slice : slices)
            {
                std::unordered_set<int> seen_t_material_ids;
                std::vector<SapDataFrame> uniqueSlice;

                for (const auto& frame : slice)
                {
                    if (seen_t_material_ids.insert(frame.t_material_id).second)
                    {
                        uniqueSlice.push_back(frame); // первая встречающаяся запись по t_material_id
                    }
                }

                if (!uniqueSlice.empty())
                {
                    uniqueMaterialSlices.push_back(std::move(uniqueSlice));
                }
            }
        }
    }

    return uniqueSlices;
}

// Вывод срезов данных по year -> higher_tm
void printSlicesForYearAndTm(const YearSlices& slices, int year, const std::string& higher_tm)
{
    auto yearIt = slices.find(year);
    if (yearIt == slices.end())
    {
        std::cout << "Год " << year << " не найден\n";
        return;
    }

    auto tmIt = yearIt->second.find(higher_tm);
    if (tmIt == yearIt->second.end())
    {
        std::cout << "higher_tm " << higher_tm << " не найден для года " << year << "\n";
        return;
    }

    const auto& sliceList = tmIt->second;
    std::cout << "Срезы для " << year << " и higher_tm = " << higher_tm << ": " << sliceList.size() << " шт.\n";

    for (size_t i = 0; i < sliceList.size(); ++i)
    {
        std::cout << "  Срез #" << i << " (" << sliceList[i].size() << " записей):\n";
        for (const auto& frame : sliceList[i])
        {
            if (frame.t_material_id == 223)
            {
                std::cout << "culture_id: " << frame.culture_id << std::endl;
                std::cout << "region_id: " << frame.region_id << std::endl;
                std::cout << "t_material_id: " << frame.t_material_id << std::endl;
                std::cout << "pu_id: " << frame.pu_id << std::endl;
                std::cout << "higher_tm: " << frame.higher_tm << std::endl;
                std::cout << "season: " << frame.season << std::endl;
                std::cout << "calendar_day: " << frame.calendar_day << std::endl;
                std::cout << "year: " << frame.year << std::endl;
                std::cout << "actual_date: " << frame.actual_date << std::endl;
                std::cout << "start_date: " << frame.start_date << std::endl;
                std::cout << "is_completed: " << frame.is_completed << std::endl;
                std::cout << "is_started: " << frame.is_started << std::endl;

                std::cout << "actual_input_dates: ";
                for (const auto& d : frame.actual_input_dates)
                    std::cout << boost::gregorian::to_simple_string(d.date)
                              << (d.is_alternative ? "(alt) " : " ");
                std::cout << std::endl;
                std::cout << "sawing_date: " << frame.sawing_date << std::endl;
                std::cout << "resawing_date: " << frame.resawing_date << std::endl;
                std::cout << "status: " << frame.status << std::endl;
                std::cout << "is_actual: " << frame.is_actual << std::endl;
                std::cout << "minimal_date: " << frame.minimal_date << std::endl;

                std::cout << "\n\n";
            }
        }
    }
}
