// Copyright 2026, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: TODO

#ifndef QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H
#define QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H

#include <algorithm>
#include <numeric>
#include <vector>
#include <parallel/algorithm>
#include <execution>
#include <boost/sort/sort.hpp>

#include "engine/idTable/IdTable.h"
#include "ips4o.hpp"


// Alternative sort implementations for `IdTable`, benchmarked against the
// production implementation in `IdTableUtils::sort` (src/index/IdTableUtils.h
// and .cpp).

namespace ad_benchmark {

// since the enum is used to iterate through and determines placement in column,
// it's best when the column-names are right beside it to make sure order matches
// for Boost: it's parallel sort attempts to take a reference to a dereferenced row. 
// Since that is a rvalue, the proxy
enum class SortMode {PERM_IPS4O, PERM_GNU, PERM_STD_PAR, PERM_BOOST,
  ROWP_IPS4O, ROWP_GNU, ROWP_STD_PAR, /*ROWP_BOOST,*/
  ROWTABLE_IPS4O, ROWTABLE_GNU, ROWTABLE_STD_PAR, ROWTABLE_BOOST,
  COUNT};
const std::vector<std::__cxx11::basic_string<char>>
  SortModeColumnNames = {"Column_amount",
    "Permutation_IPS4O_PAR", "Permutation_GNU", "Permutation_STD_PAR",
    "Permutation_BOOST",
    "RowProxy_IPS4O_PAR", "RowProxy_GNU", "RowProxy_STD_PAR", /*"RowProxy_BOOST",*/
    "RowTable_IPS4O", "RowTable_GNU", "RowTable_STD_PAR", "RowTable_BOOST"
  };

namespace detail {

struct Sorter {
  SortMode mode_;

  template <typename It, typename Comp>
  void operator()(It begin, It end, Comp comp) const {
    switch (mode_) {
      case SortMode::PERM_IPS4O: 
      case SortMode::ROWP_IPS4O:
      case SortMode::ROWTABLE_IPS4O:
        ips4o::parallel::sort(begin, end, comp);
        break;
      case SortMode::PERM_GNU:
      case SortMode::ROWP_GNU:
      case SortMode::ROWTABLE_GNU:
        __gnu_parallel::sort(begin, end, comp);
        break;
      case SortMode::PERM_STD_PAR:
      case SortMode::ROWP_STD_PAR:
      case SortMode::ROWTABLE_STD_PAR:
        std::sort(std::execution::par, begin, end, comp);
        break;
      default:
        std::runtime_error("no valid mode selected for Sorter");
    }
  }
};

// wrapper for boost sort
inline constexpr auto boostSort = [](auto begin, auto end, auto comp) {
  boost::sort::block_indirect_sort(begin, end, comp);
};

}  // namespace detail


template <int WIDTH, typename Sorter = detail::Sorter>
void sortByPermutation(IdTable* table, const std::vector<ColumnIndex>& sortCols,
        Sorter sorter) {
  IdTableStatic<WIDTH> stab = std::move(*table).toStatic<WIDTH>();
  // get columns from table as array since 
  // IdTable's [] operator uses unnecessary row-proxy
  auto cols = std::as_const(stab).getColumns();
  std::size_t numRows = stab.numRows();

  auto comparison = [&sortCols, &cols](std::size_t i, std::size_t j) {
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

template <int WIDTH, typename Sorter = detail::Sorter>
void rowProxySort(IdTable* table, const std::vector<ColumnIndex>& sortCols,
    Sorter sorter) {
  IdTableStatic<WIDTH> stab = std::move(*table).toStatic<WIDTH>();
  auto comparison = [&sortCols](const auto& row1, const auto& row2) {
    for (auto& col : sortCols) {
      if (row1[col] != row2[col]) {
        return row1[col] < row2[col];
      }
    }
    return false;
  };
  sorter(stab.begin(), stab.end(), comparison);
  *table = std::move(stab).toDynamic();
}

template <int constCols, typename Sorter = detail::Sorter>
void rowTableSort(std::vector<std::array<ValueId, constCols>>& table, const std::vector<ColumnIndex>& sortCols, Sorter sorter) {
  auto comparison = [&sortCols](const auto& row1, const auto& row2) {
    for (auto& col : sortCols) {
      if (row1[col] != row2[col]) {
        return row1[col] < row2[col];
      }
    }
    return false;
  };
  sorter(table.begin(), table.end(), comparison);
}

// rowSort overload handles visit for Row-based vs Column-based Table
template<int constCols, typename Sorter = detail::Sorter>
void rowSort(std::vector<std::array<ValueId, constCols>>& table, const std::vector<ColumnIndex>& sortCols, Sorter sorter) {
  rowTableSort<constCols>(table, sortCols, sorter);
}

// template isn't used, but still needed so overload works
template<int constCols, typename Sorter = detail::Sorter>
void rowSort(IdTable& table, const std::vector<ColumnIndex>& sortCols, Sorter sorter) {
  ad_utility::callFixedSizeVi(table.numColumns(),
                              [&table, &sortCols, &sorter](auto I){
                              rowProxySort<I>
                              (&table, sortCols, sorter);
                              });
}

}  // namespace ad_benchmark

#endif  // QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H
