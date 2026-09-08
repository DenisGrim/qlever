#include "../benchmark/infrastructure/Benchmark.h"
#include "../benchmark/infrastructure/BenchmarkMeasurementContainer.h"
#include "../benchmark/infrastructure/BenchmarkMetadata.h"
#include "../test/util/IdTableHelpers.h"
#include "util/SortVariants.h"
#include "engine/idTable/IdTable.h"
#include "index/IdTableUtils.h"
#include "ips4o.hpp"
#include <boost/sort/sort.hpp>

namespace ad_benchmark {


class IdTableSortBenchmark : public BenchmarkInterface {
 protected:
  std::vector<int> numRows_;
  std::vector<int> amount_rel_columns_;
  const std::array<int, 5> numCols_ = {1, 2, 3, 4, 5};

 public:
   IdTableSortBenchmark() {
     ad_utility::ConfigManager& config = getConfigManager();
     config.addOption("num-rows", "how many rows in every table",
         &numRows_, {10'000, 100'000, 1'000'000});
     config.addOption("amount-relevant-columns",
             "how many columns are used for sorting",
         &amount_rel_columns_, {1, 2, 3});
   }


   std::string name() const override {
     return "IdTableSortBenchmark";
   }
   
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

    ad_utility::callFixedSizeVi(numCols_[colIdx], [&](auto I) {
      for (int i = 0; i < static_cast<int>(SortMode::COUNT); i++) {
        auto table = createTable<I>(rows, numCols_[colIdx], static_cast<SortMode>(i));
        auto sortTest = [&](){
            runOneBenchmark<I>(table, static_cast<SortMode>(i), sortCols);
        };
        resultsTable.addMeasurement(colIdx, i + 1, sortTest);
      }
    });
  }

  template<int constCols>
  void runOneBenchmark(std::variant<IdTable, std::vector<std::array<ValueId, constCols>>>& table,
          SortMode mode, std::vector<ColumnIndex> sortCols) {
    switch (mode) {
      // special treatment for production
      case SortMode::PRODUCTION:
        IdTableUtils::sort(std::get<IdTable>(table), sortCols);
        break;
      // special for boost as sorter because putting it in detail::Sorter
      // won't compile
      case SortMode::PERM_BOOST: {
        IdTable& idTable = std::get<IdTable>(table);
        ad_utility::callFixedSizeVi(idTable.numColumns(),
                                    [&idTable, &sortCols](auto I) {
                                    sortByPermutation<I>
                                    (&idTable, sortCols, detail::boostSort);
                                    });
        break;
      }
      case SortMode::ROWTABLE_BOOST:
        rowSort<constCols>(
            std::get<std::vector<std::array<ValueId, constCols>>>(table),
            sortCols, detail::boostSort);
        break;


      // all modes using Permutation sort
      case SortMode::PERM_IPS4O:
      case SortMode::PERM_IPS4O_SEQ:
      case SortMode::PERM_STD:
      case SortMode::PERM_GNU:
      case SortMode::PERM_STD_PAR: {
        IdTable& idTable = std::get<IdTable>(table);
        ad_utility::callFixedSizeVi(idTable.numColumns(),
                                    [&idTable, &sortCols, &mode](auto I) {
                                    sortByPermutation<I>
                                    (&idTable, sortCols, detail::Sorter{mode});
                                    });
        break;
      }
      // rowSort has overload for vector<array> vs IdTable
      default:
        std::visit([&sortCols, &mode](auto&& tab){
            rowSort<constCols>(tab, sortCols, detail::Sorter{mode});
            }, table);
    } // switch mode
  }

  template<int i>
  std::variant<IdTable, std::vector<std::array<ValueId, i>>> createTable(int rows, int cols, SortMode mode) {
    switch (mode) {
      // all modes that need RowBasedIdTable
      case SortMode::ROWTABLE_IPS4O:
      case SortMode::ROWTABLE_STD_SEQ:
      case SortMode::ROWTABLE_IPS4O_SEQ:
      case SortMode::ROWTABLE_GNU:
      case SortMode::ROWTABLE_STD_PAR:
      case SortMode::ROWTABLE_BOOST:
        return createRowBasedValueIdTable<i>(rows);
        break;
      default:
        return createRandomlyFilledIdTable(rows, cols);
    }

  }

  template<int i>
  std::vector<std::array<ValueId, i>> createRowBasedValueIdTable(int rows) {
    std::vector<std::array<ValueId, i>> rowTable(rows);
    
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
