#pragma once
#include <vector>
#include <string>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <pqxx/pqxx>
struct ActualInputDate
{
	boost::gregorian::date date;
	bool is_alternative;
};

struct SapDataFrame
{
	int culture_id;
	int region_id;
	int t_material_id;
	int pu_id;
	std::string higher_tm;
	std::string season;
	boost::gregorian::date calendar_day;
	int planned_volume;
	int actual_volume;
	int year;

	// calc fields
	std::optional<boost::gregorian::date> actual_date;
	std::optional<boost::gregorian::date> start_date;
	bool is_completed;
	bool is_started;
	std::vector<ActualInputDate> actual_input_dates;
	std::optional<boost::gregorian::date> sawing_date;
	std::optional<boost::gregorian::date> resawing_date;
	std::optional<std::string> status;
	std::optional<std::string> is_actual;
	std::optional<boost::gregorian::date> minimal_date;
	std::optional<int> order;
};

struct SapData
{
	std::vector<SapDataFrame> data;

	explicit SapData(const pqxx::result& rows);

	SapData() = default;

	int Size();

	void Print();
};

struct IDSReseeding
{
	std::vector<int> IDs;

	explicit IDSReseeding(const pqxx::result& rows);

	int Size();

	void Print();
};

struct IDSSeeding
{
	std::vector<int> IDs;

	explicit IDSSeeding(const pqxx::result& rows);

	int Size();

	void Print();
};

// группированные срезы:
// year -> higher_tm -> vector<vector<SapDataFrame>>
using MaterialSlices = std::vector<std::vector<SapDataFrame>>;
using HigherTmSlices = std::unordered_map<std::string, MaterialSlices>;
using YearSlices = std::unordered_map<int, HigherTmSlices>;

YearSlices sliceSapData(const std::vector<SapDataFrame>& data, const IDSReseeding& reseeding);
YearSlices makeUniqueTMaterialSlices(const YearSlices& originalSlices);

void printSlicesForYearAndTm(const YearSlices& slices, int year, const std::string& higher_tm);