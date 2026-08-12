#include <db/db.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>

Database::Database(const std::string& config_file)
	: port_(0)
{
	loadConfig(config_file);
}

Database::~Database()
{
	if (conn_ && conn_->is_open())
	{
		try
		{
			// Закрываем соединение через close()
			conn_->close();
		}
		catch (const std::exception& e)
		{
			std::cerr << "[Database::~Database] Ошибка при закрытии соединения: "
				<< e.what() << std::endl;
		}
	}
}

void Database::loadConfig(const std::string& config_file)
{
	try
	{
		YAML::Node config = YAML::LoadFile(config_file);

		// Проверяем обязательные поля
		if (!config["host"] || !config["port"] || !config["user"] ||
			!config["password"] || !config["dbname"])
		{
			throw std::runtime_error("В файле конфигурации отсутствуют обязательные поля.");
		}

		host_ = config["host"].as<std::string>();
		port_ = config["port"].as<int>();
		user_ = config["user"].as<std::string>();
		password_ = config["password"].as<std::string>();
		dbname_ = config["dbname"].as<std::string>();

		// Опциональное поле sslmode
		if (config["sslmode"])
		{
			sslmode_ = config["sslmode"].as<std::string>();
		}
		else
		{
			sslmode_.clear();
		}

	}
	catch (const YAML::BadFile& e)
	{
		throw std::runtime_error("Не удалось открыть YAML-файл: " + std::string(e.what()));
	}
	catch (const YAML::ParserException& e)
	{
		throw std::runtime_error("Ошибка парсинга YAML-файла: " + std::string(e.what()));
	}
	// прочие исключения (например, отсутствует поле) просто пробрасываем
}

void Database::connect()
{
	if (conn_ && conn_->is_open())
	{
		// Уже подключены, ничего не делаем
		return;
	}

	// Собираем строку подключения в формате libpq
	std::ostringstream conn_str;
	conn_str << "host=" << host_
		<< " port=" << port_
		<< " user=" << user_
		<< " password=" << password_
		<< " dbname=" << dbname_;
	if (!sslmode_.empty())
	{
		conn_str << " sslmode=" << sslmode_;
	}

	try
	{
		// Создаём соединение
		conn_ = std::make_unique<pqxx::connection>(conn_str.str());

		if (!conn_->is_open())
		{
			throw std::runtime_error("Не удалось открыть соединение с базой данных.");
		}
	}
	catch (const pqxx::broken_connection& e)
	{
		throw std::runtime_error("Ошибка соединения с PostgreSQL: " + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		throw std::runtime_error("Ошибка при попытке connect(): " + std::string(e.what()));
	}
}

pqxx::connection& Database::getConnection()
{
	if (!conn_ || !conn_->is_open())
	{
		throw std::runtime_error("Соединение с БД не установлено. Вызовите connect() перед использованием.");
	}
	return *conn_;
}

pqxx::result Database::fetchInitialDataRaw()
{
	pqxx::work txn(*conn_);

	const std::string query = R"(
        SELECT
            culture_id,
            t_material_id,
            region_id,
            season,
            region_date,
            input_operation_order,
            input_deadline,
            noinput_deadline,
            "order",
            year
        FROM static_initial_data_control_operations ORDER BY "order" ASC, culture_id ASC, region_id ASC
    )";

	pqxx::result result = txn.exec(query);
	txn.commit();
	return result;
}

pqxx::result Database::fetchSapControlOperationsRaw()
{
	pqxx::work txn(*conn_);

	const std::string query = R"(
        SELECT
            culture_id,
            region_id,
            t_material_id,
            pu_id,
            higher_tm,
            season,
            calendar_day,
            planned_volume,
            actual_volume,
            year
        FROM sap_control_operations_source ORDER BY calendar_day, id
    )";

	pqxx::result result = txn.exec(query);
	txn.commit();
	return result;
}

pqxx::result Database::fetchSapSugarBeetSawingDatesOpsRaw()
{
	pqxx::work txn(*conn_);

	const std::string query = R"(
        SELECT
            culture_id,
            region_id,
            t_material_id,
            pu_id,
            higher_tm,
            season,
            calendar_day,
            planned_volume,
            actual_volume,
            year
        FROM sap_sugar_beet_sawing_dates_ops_source ORDER BY calendar_day, id
    )";

	pqxx::result result = txn.exec(query);
	txn.commit();
	return result;
}

pqxx::result Database::fetchSapIDSSeedingRaw()
{
	pqxx::work txn(*conn_);

	const std::string query = R"(
       SELECT
       id
       FROM sap_operations
       WHERE (operation_name ILIKE '%Посев с%' OR operation_name ILIKE '%Посев без%')
    )";

	pqxx::result result = txn.exec(query);
	txn.commit();
	return result;
}

pqxx::result Database::fetchSapIDSReseedingRaw()
{
	pqxx::work txn(*conn_);

	const std::string query = R"(
       SELECT
       id
       FROM sap_operations
       WHERE (operation_name ILIKE '%Пересев с%' OR operation_name ILIKE '%Пересев без%')
    )";

	pqxx::result result = txn.exec(query);
	txn.commit();
	return result;
}

void Database::insertIntoControlOperationsAggregated(const YearSlices& uniqueSlices)
{
    pqxx::work txn(*conn_);
    try
    {

        auto stream = pqxx::stream_to::table(
            txn,
            {"control_operations_aggregated_new"},
            {
                "culture_id", "region_id", "t_material_id", "pu_id",
                "higher_tm", "season", "calendar_day", "year",
                "actual_date", "start_date", "is_completed", "is_started",
                "sawing_date", "resawing_date", "status", "is_actual",
                "minimal_date", "order"
            }
        );

        for (const auto& [year, higherTmMap] : uniqueSlices)
        {
            for (const auto& [higherTm, materialSlices] : higherTmMap)
            {
                for (const auto& slice : materialSlices)
                {
                    for (const auto& frame : slice)
                    {
                        // Конвертируем optional<date> в optional<string> для libpqxx
                        auto optDateToString = [](const std::optional<boost::gregorian::date>& d)
                            -> std::optional<std::string>
                        {
                            return d.has_value() ? std::optional<std::string>(to_iso_string(*d))
                                                 : std::nullopt;
                        };

                        stream.write_values(
                            frame.culture_id,
                            frame.region_id,
                            frame.t_material_id,
                            frame.pu_id,
                            higherTm,
                            frame.season,
                            to_iso_string(frame.calendar_day),
                            year,
                            optDateToString(frame.actual_date),
                            optDateToString(frame.start_date),
                            frame.is_completed,
                            frame.is_started,
                            optDateToString(frame.sawing_date),
                            optDateToString(frame.resawing_date),
                            frame.status,
                            frame.is_actual,
                            optDateToString(frame.minimal_date),
                            frame.order
                        );
                    }
                }
            }
        }

        stream.complete();
        txn.commit();
    }
    catch (const std::exception& e)
    {
        txn.abort();
        throw;
    }
}

void Database::insertIntoSapSugarBeetSawingDates(const YearSlices& uniqueSlices)
{
    pqxx::work txn(*conn_);
    try
    {
        auto stream = pqxx::stream_to::table(
            txn,
            {"sap_sugar_beet_sawing_dates"},
            {"culture_id", "pu_id", "higher_tm", "year", "sawing_date"}
        );

        auto optDateToString = [](const std::optional<boost::gregorian::date>& d)
            -> std::optional<std::string>
        {
            return d.has_value() ? std::optional<std::string>(to_iso_string(*d))
                                 : std::nullopt;
        };

        for (const auto& [year, higherTmMap] : uniqueSlices)
        {
            for (const auto& [higherTm, materialSlices] : higherTmMap)
            {
                for (const auto& slice : materialSlices)
                {
                    for (const auto& frame : slice)
                    {
                        stream.write_values(
                            frame.culture_id,
                            frame.pu_id,
                            higherTm,
                            year,
                            optDateToString(frame.sawing_date)
                        );
                    }
                }
            }
        }

        stream.complete();
        txn.commit();
    }
    catch (const std::exception& e)
    {
        txn.abort();
        throw;
    }
}


void Database::truncateAndRestartIdentity(std::string table_name)
{
	pqxx::work txn(*conn_);

	const std::string query = R"(TRUNCATE TABLE )" + table_name + R"( RESTART IDENTITY)";

	txn.exec(query);
	txn.commit();
}

