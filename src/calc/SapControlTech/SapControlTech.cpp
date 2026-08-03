#include <calc/SapControlTech/SapControlTech.hpp>
#include <db/db.hpp>
#include <data_struct/SapData.hpp>
#include <data_struct/InitialData.hpp>
#include <calc/SapControlTech/CalcMinimalPlannedDate.hpp>
#include <calc/SapControlTech/CalcActualDate.hpp>
#include <calc/SapControlTech/CalcActualDateCompleteEntryOperation.hpp>
#include <calc/SapControlTech/CalcSawingDate.hpp>
#include <calc/SapControlTech/CalcResawingDate.hpp>
#include <calc/SapControlTech/CalcMinimalDate.hpp>
#include <chrono>

int calcSapControlAggregated()
{
    try
    {
        Database db("config/db_config.yml");
        db.connect();

        // Выгрузка данных из таблицы static_initial_data
        pqxx::result rawInitData = db.fetchInitialDataRaw();
        InitialData initialData(rawInitData);

        // Выгрузка данных из таблицы sap_control_operations
        pqxx::result rawSapDataAll = db.fetchSapControlOperationsRaw();
        SapData sapDataAll(rawSapDataAll);

        // Получаем id операций с пересевами
        pqxx::result rawIDSReseeding = db.fetchSapIDSReseedingRaw();
        IDSReseeding idsReseeding(rawIDSReseeding);

        // Получаем id операций с посевами
        pqxx::result rawIDSSeeding = db.fetchSapIDSSeedingRaw();
        IDSSeeding idsSeeding(rawIDSSeeding);

        clock_t calc_start = clock();

        calcMinimalPlannedDate(initialData);

        // Разделение данных
        YearSlices sapDataSlices = sliceSapData(sapDataAll.data, idsReseeding);

        // Создаем структуру в которой каждый срез содержит только записи с уникальными t_material на основе первого вхождения (уже sort по calendar_day)
        YearSlices sapDataUniqueTMaterialSlices = makeUniqueTMaterialSlices(sapDataSlices);

        calcActualDate(sapDataUniqueTMaterialSlices, sapDataSlices);

        calcActualInputDates(sapDataUniqueTMaterialSlices, initialData);

        calcSawingDate(sapDataUniqueTMaterialSlices, idsSeeding);

        calcResawingDate(sapDataUniqueTMaterialSlices, idsReseeding);

        calcMinimalDate(sapDataUniqueTMaterialSlices, initialData);

        clock_t calc_end = clock();

        double calc_seconds = (double)(calc_end - calc_start) / CLOCKS_PER_SEC;

        std::cout << "Calc time: " << calc_seconds << " seconds" << std::endl;

        clock_t uploading_start = clock();

        //printSlicesForYearAndTm(sapDataUniqueTMaterialSlices, 2025, "BL-07-01-01-0002");

        db.truncateAndRestartIdentity("control_operations_aggregated_new");

        db.insertIntoControlOperationsAggregated(sapDataUniqueTMaterialSlices);

        clock_t uploading_end = clock();

        double uploading_seconds = (double)(uploading_end - uploading_start) / CLOCKS_PER_SEC;

        std::cout << "Unloading data into PostgreSQL: " << uploading_seconds << " seconds" << std::endl;


    }
    catch (const std::exception& e)
    {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
