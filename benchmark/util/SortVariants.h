// Copyright 2026, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: TODO

#ifndef QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H
#define QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H

#include <algorithm>
#include <numeric>
#include <vector>

#include "engine/idTable/IdTable.h"
#include "ips4o.hpp"


// Alternative sort implementations for `IdTable`, benchmarked against the
// production implementation in `IdTableUtils::sort` (src/index/IdTableUtils.h
// and .cpp).

namespace ad_benchmark {

namespace detail {
// Default `Sorter` for `sortByPermutation` below: just forwards to std::sort
struct StdSort {
  template <typename It, typename Comp>
  void operator()(It begin, It end, Comp comp) const {
    std::sort(begin, end, comp);
  }
};

// `Sorter` for `sortByPermutation` below that forwards to
// `ips4o::parallel::sort`.
struct Ips4oParallelSort {
  template <typename It, typename Comp>
  void operator()(It begin, It end, Comp comp) const {
    ips4o::parallel::sort(begin, end, comp);
  }
};
}  // namespace detail

template <int WIDTH, typename Sorter = detail::StdSort>
void sortByPermutation(IdTable* table, const std::vector<ColumnIndex>& sortCols,
        Sorter sorter = {}) {
  IdTableStatic<WIDTH> stab = std::move(*table).toStatic<WIDTH>();
  // get columns from table as array since 
  // IdTable's [] operator uses unnecessary row-proxy
  auto cols = std::as_const(stab).getColumns();
  std::size_t numRows = stab.numRows();

  auto comparison = [&sortCols, &stab](std::size_t i, std::size_t j) {
    for (auto& col : sortCols) {
      if (cols[col][i] != cols[col][j]) {
        return cols[col][i] < cols[col][j];
      }
    }
    return false;
  };
  //indentity permutation
  std::vector<std::size_t> perm(numRows);
  std::iota(perm.begin(), perm.end(), 0);
  
  sorter(perm.begin(), perm.end(), comparison);

  // apply permutation into new table
  IdTableStatic<WIDTH> result{stab.numColumns(), stab.getAllocator()};
  result.resize(numRows);

  for (size_t col = 0; col < stab.numColumns(); ++col) {
    auto src = stab.getColumn(col);
    auto dst = result.getColumn(col);
    for (size_t i = 0; i < numRows; ++i) {
      dst[i] = src[perm[i]];
    }
  }
  *table = std::move(result).toDynamic();
}

template <int WIDTH>
void ips4oSort(IdTable* table, const std::vector<ColumnIndex>& sortCols) {
  IdTableStatic<WIDTH> stab = std::move(*table).toStatic<WIDTH>();
  auto comparison = [&sortCols](const auto& row1, const auto& row2) {
    for (auto& col : sortCols) {
      if (row1[col] != row2[col]) {
        return row1[col] < row2[col];
      }
    }
    return false;
  };
  ips4o::parallel::sort(stab.begin(), stab.end(), comparison);
  *table = std::move(stab).toDynamic();
}

}  // namespace ad_benchmark

#endif  // QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H
