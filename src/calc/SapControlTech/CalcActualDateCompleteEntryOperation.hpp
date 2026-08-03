#pragma once
#include <data_struct/SapData.hpp>
#include <data_struct/InitialData.hpp>

// For each SAP frame, compute the actual date implied by every input dependency
// (parent operation actual_date + dependency deadline) and store them on the frame.
void calcActualInputDates(YearSlices& uniqueSlices, const InitialData& initData);
