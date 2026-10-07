// Copyright 2026, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: TODO

#ifndef QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H
#define QLEVER_BENCHMARK_IDTABLESORTBENCHMARK_SORTVARIANTS_H

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <parallel/algorithm>
#include <execution>
#include <omp.h>
#include <thread>
#include <boost/asio/thread_pool.hpp>
#include <boost/sort/sort.hpp>

#include "engine/idTable/IdTable.h"
#include "ips4o.hpp"
#include "util/blockSort/BlockIndirectSort.h"


// Alternative sort implementations for `IdTable`, benchmarked against the
// production implementation in `IdTableUtils::sort` (src/index/IdTableUtils.h
// and .cpp).

namespace ad_benchmark {

// The enum is used to iterate through the sort modes and determines their
// placement in the results table. Both the enum and the column names are
// generated from this single list (X-macro), so their order always matches.
// ROWP_BOOST_BIS is left out: boost's parallel sort attempts to take a
// reference to a dereferenced row. Since that is an rvalue, the proxy
// doesn't compile.
// The *_QL_BIS modes use QLever's port of boost's block_indirect_sort
// (src/util/blockSort), which also works with proxy references.
#define QLEVER_SORT_MODES(X) \
  X(PERM_IPS4O_PAR)          \
  X(PERM_GNU)                \
  X(PERM_STD_PAR)            \
  X(PERM_BOOST_BIS)          \
  X(PERM_BOOST_SS)           \
  X(PERM_BOOST_PSS)          \
  X(PERM_QL_BIS)             \
  X(ROWP_IPS4O_PAR)          \
  X(ROWP_GNU)                \
  X(ROWP_STD_PAR)            \
  X(ROWP_BOOST_SS)           \
  X(ROWP_BOOST_PSS)          \
  X(ROWP_QL_BIS)             \
  X(ROWTABLE_IPS4O_PAR)      \
  X(ROWTABLE_GNU)            \
  X(ROWTABLE_STD_PAR)        \
  X(ROWTABLE_BOOST_BIS)      \
  X(ROWTABLE_BOOST_SS)       \
  X(ROWTABLE_BOOST_PSS)      \
  X(ROWTABLE_QL_BIS)

#define QLEVER_SORT_MODE_ENUM_ENTRY(name) name,
#define QLEVER_SORT_MODE_NAME_ENTRY(name) #name,

enum class SortMode { QLEVER_SORT_MODES(QLEVER_SORT_MODE_ENUM_ENTRY) COUNT };

// First column holds the number of columns of the table, the remaining ones
// are the sort modes in enum order (column index = mode index + 1).
inline const std::vector<std::string> SortModeColumnNames = {
    "Column_amount", QLEVER_SORT_MODES(QLEVER_SORT_MODE_NAME_ENTRY)};

#undef QLEVER_SORT_MODE_ENUM_ENTRY
#undef QLEVER_SORT_MODE_NAME_ENTRY
#undef QLEVER_SORT_MODES

namespace detail {

// Number of threads and the thread pool used by QLever's blockIndirectSort.
// The calling thread blocks during the sort, so the pool must not contain it.
inline uint32_t blockSortNumThreads() {
  return std::max(std::thread::hardware_concurrency(), 2u);
}

inline boost::asio::thread_pool& blockSortPool() {
  static boost::asio::thread_pool pool{blockSortNumThreads()};
  return pool;
}

struct Sorter {
  SortMode mode_;

  template <typename It, typename Comp>
  void operator()(It begin, It end, Comp comp) const {
    switch (mode_) {
      case SortMode::PERM_BOOST_SS:
      case SortMode::ROWP_BOOST_SS:
      case SortMode::ROWTABLE_BOOST_SS:
        boost::sort::sample_sort(begin, end, comp);
        break;
      case SortMode::PERM_BOOST_PSS:
      case SortMode::ROWP_BOOST_PSS:
      case SortMode::ROWTABLE_BOOST_PSS:
        boost::sort::parallel_stable_sort(begin, end, comp);
        break;
      case SortMode::PERM_IPS4O_PAR: 
      case SortMode::ROWP_IPS4O_PAR:
      case SortMode::ROWTABLE_IPS4O_PAR:
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
      case SortMode::PERM_QL_BIS:
      case SortMode::ROWP_QL_BIS:
      case SortMode::ROWTABLE_QL_BIS:
        ad_utility::blockSort::blockIndirectSort(
            ql::ranges::subrange(begin, end), comp, blockSortNumThreads(),
            blockSortPool().get_executor());
        break;
      default:
        throw std::runtime_error("no valid mode selected for Sorter");
    }
  }
};

// wrapper for boost sort
inline constexpr auto boostSort = [](auto begin, auto end, auto comp) {
  boost::sort::block_indirect_sort(begin, end, comp);
};

}  // namespace detail

template <int WIDTH>
IdTableStatic<WIDTH> copyWithAppliedPermutation(IdTableStatic<WIDTH>& stab,
  std::vector<std::size_t>& perm) {
  IdTableStatic<WIDTH> result{stab.numColumns(), stab.getAllocator()};
  result.resize(stab.numRows());

  for (size_t col = 0; col < stab.numColumns(); ++col) {
    auto src = stab.getColumn(col);
    auto dst = result.getColumn(col);
    const size_t numRows = stab.numRows();
#pragma omp parallel for
    for (size_t i = 0; i < numRows; ++i) {
      dst[i] = src[perm[i]];
    }
  }
  return result;
}

template <int WIDTH, typename Sorter = detail::Sorter>
void sortByPermutation(IdTable* table, const std::vector<ColumnIndex>& sortCols,
        Sorter sorter) {
  IdTableStatic<WIDTH> stab = std::move(*table).toStatic<WIDTH>();
  // get columns from table as array since
  // IdTable's [] operator uses row-proxy
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

  *table = std::move(copyWithAppliedPermutation<WIDTH>(stab, perm)).toDynamic();
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
