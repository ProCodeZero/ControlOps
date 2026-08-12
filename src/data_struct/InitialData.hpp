#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <pqxx/pqxx>
#include <utils/InputAggregation.hpp>

struct KeyOrder4
{
    int culture_id;
    int region_id;
    int order;
    int year;

    bool operator==(KeyOrder4 const& o) const
    {
        return culture_id == o.culture_id
            && region_id == o.region_id
            && order == o.order
            && year == o.year;
    }
};

struct KeyOrder4Hash
{
    std::size_t operator()(KeyOrder4 const& key) const noexcept
    {
        std::size_t h1 = static_cast<std::size_t>(key.culture_id) * 73856093;
        std::size_t h2 = static_cast<std::size_t>(key.region_id) * 83492791;
        std::size_t h3 = static_cast<std::size_t>(key.order) * 19349663;
        std::size_t h4 = static_cast<std::size_t>(key.year) * 49979693;

        return (h1 ^ h2 ^ h3 ^ h4);
    }
};

struct KeyCRTYS5 
{
    int culture_id;
    int region_id;
    int t_material_id;
    int year;
    std::string season;

    bool operator==(const KeyCRTYS5& other) const 
    {
        return culture_id == other.culture_id &&
            region_id == other.region_id &&
            t_material_id == other.t_material_id &&
            year == other.year &&
            season == other.season;
    }
};

struct KeyCRTYS5Hash 
{
    std::size_t operator()(const KeyCRTYS5& key) const noexcept 
    {
        std::size_t h1 = static_cast<std::size_t>(key.culture_id) * 73856093;
        std::size_t h2 = static_cast<std::size_t>(key.region_id) * 83492791;
        std::size_t h3 = static_cast<std::size_t>(key.t_material_id) * 19349663;
        std::size_t h4 = static_cast<std::size_t>(key.year) * 49979693;
        std::size_t h5 = std::hash<std::string>{}(key.season);

        return h1 ^ h2 ^ h3 ^ h4 ^ h5;
    }
};


struct PlannedDates
{
	std::optional<boost::gregorian::date> planned_date;
	std::optional<boost::gregorian::date> minimal_planned_date;
};

struct InitialDataFrame
{
	int culture_id;
	int t_material_id;
	int region_id;
	std::string season;
	std::optional<boost::gregorian::date> region_date;
	std::vector<InputOperation> input_operations;
	std::optional<int> noinput_deadline;
	int order;
	int year;

    // calc struct
	PlannedDates planned_dates;
};

struct InitialData
{
	std::vector<InitialDataFrame> data;

	std::unordered_map<KeyOrder4, int, KeyOrder4Hash> order_index_map;

    std::unordered_map<KeyCRTYS5, int, KeyCRTYS5Hash> CRTYS_index_map;

	explicit InitialData(const pqxx::result& rows);

	int Size();

	void Print();
};