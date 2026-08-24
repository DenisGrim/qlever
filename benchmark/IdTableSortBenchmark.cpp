#include "../benchmark/infrastructure/Benchmark.h"
#include "../benchmark/infrastructure/BenchmarkMeasurementContainer.h"
#include "../benchmark/infrastructure/BenchmarkMetadata.h"
#include "../test/util/IdTableHelpers.h"
#include "util/SortVariants.h"
#include "engine/idTable/IdTable.h"
#include "index/IdTableUtils.h"
#include "ips4o.hpp"

namespace ad_benchmark {

enum class Mode {PERM_IPS4O, PERM, IPS4O, PRODUCTION, COUNT};

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
         &numCols_, {1, 2, 3, 4, 5});
     config.addOption("amount-relevant-columns",
             "how many columns are used for sorting",
         &amount_rel_columns_, {1, 2, 3, 4});
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
                 {"Column_amount", "Production", "Permutation",
                 "Production_IPS4O", "Permutation_IPS4O"}
                 );
         // loop over index of cols because it determines placement in results table
         for (size_t colIdx = 0; colIdx < numCols_.size(); colIdx++) {
           resultsTable.addRow();
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

    for (int i = 0; i < static_cast<int>(Mode::COUNT); i++) {
      IdTable table = createRandomlyFilledIdTable(rows, numCols_[colIdx]);
      auto sortTest = [&](){
          runOneBenchmark(table, static_cast<Mode>(i), sortCols);
      };
      resultsTable.addMeasurement(colIdx, i + 1, sortTest);
    }
  }
  void runOneBenchmark(IdTable& table, Mode mode,
          std::vector<ColumnIndex> sortCols) {
    switch (mode) {
      case Mode::PERM_IPS4O:
        ad_utility::callFixedSizeVi(table.numColumns(),
                                    [&table, &sortCols](auto I) {
                                    sortByPermutation<I>(&table, sortCols,
                                            detail::Ips4oParallelSort{});
                                    });
        break;
      case Mode::PERM:
        ad_utility::callFixedSizeVi(table.numColumns(),
                                    [&table, &sortCols](auto I) {
                                    sortByPermutation<I>(&table, sortCols);
                                    });
        break;
      case Mode::IPS4O:
        ad_utility::callFixedSizeVi(table.numColumns(),
                                    [&table, &sortCols](auto I){
                                    ips4oSort<I>(&table, sortCols);
                                    });
        break;
      case Mode::PRODUCTION:
        IdTableUtils::sort(table, sortCols);
        break;
      case Mode::COUNT:
        break;
    }
  }

};



AD_REGISTER_BENCHMARK(IdTableSortBenchmark);

}  // namespace ad_benchmark
