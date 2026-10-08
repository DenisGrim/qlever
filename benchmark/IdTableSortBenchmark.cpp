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
  std::vector<int> numThreads_;
  std::vector<int> amount_rel_columns_;
  const std::array<int, 2> numCols_ = {1,5};

 public:
   IdTableSortBenchmark() {
     ad_utility::ConfigManager& config = getConfigManager();
     config.addOption("num-rows", "how many rows in every table",
         &numRows_, {1'000'000});
     // auto fills the vector with 1-hardware_concurrency
     config.addOption("num-threads", "how many threads are used (none=auto)",
         &numThreads_, {});
     config.addOption("amount-relevant-columns",
             "how many columns are used for sorting",
         &amount_rel_columns_, {1});
   }


   std::string name() const override {
     return "IdTableSortBenchmark";
   }
   
   BenchmarkResults runAllBenchmarks() override {
     BenchmarkResults results{};
     if (numThreads_.size() == 0) {
       int n = static_cast<int>(std::thread::hardware_concurrency());

       for (int threads = 1; threads <= n; threads *= 2) {
         numThreads_.push_back(threads);
       }
       // also measure with all threads if n isn't a power of two
       if (!numThreads_.empty() && numThreads_.back() != n) {
         numThreads_.push_back(n);
       }
     }
    
   
     for (int arc : amount_rel_columns_) {
       auto& group = results.addGroup("amount_sorting_columns: "
           + std::to_string(arc));
       std::vector<ColumnIndex> sortCols(arc);
       std::iota(sortCols.begin(), sortCols.end(), 0);

       for (uint32_t numThreads : numThreads_) {
         for (auto rows : numRows_) {
           // Table for each numThread + numRow combo
           std::string tableLabel = "threads: " + std::to_string(numThreads) +
             ", rows: " + std::to_string(rows);
           auto& resultsTable = group.addTable(tableLabel, {}, SortModeColumnNames);
        
           // loop over index of cols because it determines 
           // placement in results table
           for (size_t colIdx = 0; colIdx < numCols_.size(); colIdx++) {
             resultsTable.addRow();
             // at least as many column as should be relevant
             if (arc > numCols_[colIdx]) {
               continue;
             }
             resultsTable.setEntry(colIdx, 0, std::to_string(numCols_[colIdx]));
             addEverySortMethodToResults(resultsTable, rows, colIdx,
                 sortCols, numThreads);
           }
         }
       }
     }

     return results;
   }

 private:
  void addEverySortMethodToResults(auto& resultsTable, int rows, int colIdx,
      std::vector<ColumnIndex>& sortCols, uint32_t numThreads) {

    ad_utility::callFixedSizeVi(numCols_[colIdx], [&](auto I) {
      for (int i = 0; i < static_cast<int>(SortMode::COUNT); i++) {
        auto table = createTable<I>(rows, numCols_[colIdx], static_cast<SortMode>(i));
        auto sortTest = [&](){
            runOneBenchmark<I>(table, static_cast<SortMode>(i), sortCols, numThreads);
        };
        resultsTable.addMeasurement(colIdx, i + 1, sortTest);
      }
    });
  }

  template<int constCols>
  void runOneBenchmark(std::variant<IdTable, 
    std::vector<std::array<ValueId, constCols>>>& table,
          SortMode mode, std::vector<ColumnIndex> sortCols, uint32_t numThreads) {
    switch (mode) {
      // special for boost as sorter because putting it in detail::Sorter
      // won't compile
      case SortMode::PERM_BOOST_BIS: {
        IdTable& idTable = std::get<IdTable>(table);
        ad_utility::callFixedSizeVi(idTable.numColumns(),
                                    [&idTable, &sortCols, numThreads](auto I) {
                                    sortByPermutation<I>
                                    (&idTable, sortCols,
                                     detail::BoostSorter{numThreads},
                                     numThreads,
                                     "PERM_BOOST_BIS");
                                    });
        break;
      }
      case SortMode::ROWTABLE_BOOST_BIS:
        rowSort<constCols>(
            std::get<std::vector<std::array<ValueId, constCols>>>(table),
            sortCols, detail::BoostSorter{numThreads});
        break;


      // all modes using Permutation sort
      case SortMode::PERM_BOOST_SS:
      case SortMode::PERM_BOOST_PSS:
      case SortMode::PERM_QL_BIS:
      case SortMode::PERM_IPS4O_PAR:
      case SortMode::PERM_GNU:
      case SortMode::PERM_STD_PAR: {
        IdTable& idTable = std::get<IdTable>(table);
        std::string label = SortModeColumnNames.at(static_cast<size_t>(mode) + 1);
        ad_utility::callFixedSizeVi(idTable.numColumns(),
                                    [&idTable, &sortCols, &mode, &label,
                                     numThreads](auto I) {
                                    sortByPermutation<I>
                                    (&idTable, sortCols,
                                     detail::Sorter{mode, numThreads},
                                     numThreads,
                                     label);
                                    });
        break;
      }
      // rowSort has overload for vector<array> vs IdTable
      default:
        std::visit([&sortCols, &mode, numThreads](auto&& tab){
            rowSort<constCols>(tab, sortCols,
                               detail::Sorter{mode, numThreads});
            }, table);
    } // switch mode
  }

  template<int i>
  std::variant<IdTable, std::vector<std::array<ValueId, i>>> createTable(int rows, int cols, SortMode mode) {
    switch (mode) {
      // all modes that need RowBasedIdTable
      case SortMode::ROWTABLE_IPS4O_PAR:
      case SortMode::ROWTABLE_GNU:
      case SortMode::ROWTABLE_STD_PAR:
      case SortMode::ROWTABLE_BOOST_BIS:
      case SortMode::ROWTABLE_BOOST_PSS:
      case SortMode::ROWTABLE_BOOST_SS:
      case SortMode::ROWTABLE_QL_BIS:
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
