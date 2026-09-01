#include "../benchmark/infrastructure/Benchmark.h"
#include "../benchmark/infrastructure/BenchmarkMeasurementContainer.h"
#include "../benchmark/infrastructure/BenchmarkMetadata.h"
#include "../test/util/IdTableHelpers.h"
#include "util/SortVariants.h"
#include "engine/idTable/IdTable.h"
#include "index/IdTableUtils.h"
#include "ips4o.hpp"

namespace ad_benchmark {


class IdTableSortBenchmark : public BenchmarkInterface {
 protected:
  std::vector<int> numRows_;
  std::vector<int> numCols_;
  std::vector<int> amount_rel_columns_;

 public:
   IdTableSortBenchmark() {
     ad_utility::ConfigManager& config = getConfigManager();
     config.addOption("num-rows", "how many rows in every table",
         &numRows_, {10'000, 100'000, 1'000'000});
     config.addOption("num-cols", "how many cols in every table",
         &numCols_, {5});
     config.addOption("amount-relevant-columns",
             "how many columns are used for sorting",
         &amount_rel_columns_, {1, 2, 3});
   }


   std::string name() const override {
     return "IdTableSortBenchmark";
   }
   
   // Required. This is where you actually measure things, using the
   // `BenchmarkResults` passed around by value/reference. See
   // `benchmark/Usage.md` and `benchmark/BenchmarkExamples.cpp` for the full
   // feature set (single measurements, groups, tables).
   BenchmarkResults runAllBenchmarks() override {
     BenchmarkResults results{};
   

     for (int arc : amount_rel_columns_) {
       auto& group = results.addGroup("amount_sorting_columns: "
           + std::to_string(arc));
       std::vector<ColumnIndex> sortCols(arc);
       std::iota(sortCols.begin(), sortCols.end(), 0);

       for (auto rows : numRows_) {
         auto& resultsTable = group.addTable(std::to_string(rows), {},
             SortModeColumnNames);

         // loop over index of cols because it determines 
         // placement in results table
         for (size_t colIdx = 0; colIdx < numCols_.size(); colIdx++) {
           resultsTable.addRow();
           // at least as many column as should be relevant
           if (arc > numCols_[colIdx]) {
             continue;
           }
           resultsTable.setEntry(colIdx, 0, std::to_string(numCols_[colIdx]));
           addEverySortMethodToResults(resultsTable, rows, colIdx, sortCols);
         }
       }
     }

     return results;
   }

 private:
  void addEverySortMethodToResults(auto& resultsTable, int rows, int colIdx,
      std::vector<ColumnIndex>& sortCols) {

    for (int i = 0; i < static_cast<int>(SortMode::COUNT); i++) {
      // TODO wip early out if columns != 5 and SortMode needs RowBasedTable
      std::variant<IdTable, std::vector<std::array<ValueId, 5>>>
        table = createTable(rows, _numCols[colIdx], static_cast<SortMode>(i));
      auto sortTest = [&](){
          runOneBenchmark(table, static_cast<SortMode>(i), sortCols);
      };
      resultsTable.addMeasurement(colIdx, i + 1, sortTest);
    }
  }

  void runOneBenchmark(IdTable& table, SortMode mode,
          std::vector<ColumnIndex> sortCols) {
    switch (mode) {
      // special treatment for production
      case SortMode::PRODUCTION:
        IdTableUtils::sort(table, sortCols);
        break;

      // all modes using Permutation sort
      case SortMode::PERM_IPS4O:
      case SortMode::PERM_IPS4O_SEQ:
      case SortMode::PERM_STD:
      case SortMode::PERM_GNU:
      case SortMode::PERM_STD_PAR:
        ad_utility::callFixedSizeVi(table.numColumns(),
                                    [&table, &sortCols, &mode](auto I) {
                                    sortByPermutation<I>
                                    (&table, sortCols, detail::Sorter{mode});
                                    });
        break;
      // rowSort has overload for vector<array> vs IdTable
      default:
        std::visit([&sortCols, &mode](auto&& tab){
            rowSort(tab, sort, detail::Sorter{mode});
            }, table);
    } // switch mode
  }

  std::variant<IdTable, std::vector<std::array<ValueId, 5>>> createTable(int rows, int cols, SortMode mode) {
    switch (mode) {
      // all modes that need RowBasedIdTable
      case SortMode::ROWTABLE_IPS4O:
      case SortMode::ROWTABLE_STD_SEQ:
      case SortMode::ROWTABLE_IPS4O_SEQ:
      case SortMode::ROWTABLE_GNU:
      case SortMode::ROWTABLE_STD_PAR:
        return createRowBasedValueIdTable(rows);
        break;
      default:
        return createRandomlyFilledIdTable(rows, cols);
    }

  }

  std::vector<std::array<ValueId, 5>> createRowBasedValueIdTable(int rows) {
    std::vector<std::array<ValueId, 5>> rowTable(rows);
    
    ad_utility::SlowRandomIntGenerator<size_t> randomNumberGenerator(
        0, ValueId::maxIndex);
    std::function<ValueId()> valueIdGenerator = [&randomNumberGenerator]() {
      return ad_utility::testing::VocabId(randomNumberGenerator());
    };
    
    for (auto& row : rowTable) {
      for (auto& entry : row) {
        entry = valueIdGenerator();
      }
    }

    return rowTable;
  }

};



AD_REGISTER_BENCHMARK(IdTableSortBenchmark);

}  // namespace ad_benchmark
