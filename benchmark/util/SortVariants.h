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
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <omp.h>
#include <thread>
#include <boost/asio/thread_pool.hpp>
#include <boost/sort/sort.hpp>
#include <tbb/global_control.h>

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

// Thread pool used by QLever's blockIndirectSort, one per thread count, so
// that the pool is run by exactly `numThreads` threads. The calling thread
// blocks during the sort, so the pool must not contain it (that's also why a
// pool with a single thread is fine).
inline boost::asio::thread_pool& blockSortPool(uint32_t numThreads) {
  static std::map<uint32_t, std::unique_ptr<boost::asio::thread_pool>> pools;
  auto& pool = pools[numThreads];
  if (!pool) {
    pool = std::make_unique<boost::asio::thread_pool>(numThreads);
  }
  return *pool;
}

struct Sorter {
  SortMode mode_;
  uint32_t numThreads_;

  template <typename It, typename Comp>
  void operator()(It begin, It end, Comp comp) const {
    switch (mode_) {
      case SortMode::PERM_BOOST_SS:
      case SortMode::ROWP_BOOST_SS:
      case SortMode::ROWTABLE_BOOST_SS:
        boost::sort::sample_sort(begin, end, comp, numThreads_);
        break;
      case SortMode::PERM_BOOST_PSS:
      case SortMode::ROWP_BOOST_PSS:
      case SortMode::ROWTABLE_BOOST_PSS:
        boost::sort::parallel_stable_sort(begin, end, comp, numThreads_);
        break;
      case SortMode::PERM_IPS4O_PAR: 
      case SortMode::ROWP_IPS4O_PAR:
      case SortMode::ROWTABLE_IPS4O_PAR:
        ips4o::parallel::sort(begin, end, comp, static_cast<int>(numThreads_));
        break;
      case SortMode::PERM_GNU:
      case SortMode::ROWP_GNU:
      case SortMode::ROWTABLE_GNU:
        // `default_parallel_tag` is what `__gnu_parallel::sort` uses when no
        // tag is given, so this is the same algorithm with a fixed thread count.
        __gnu_parallel::sort(begin, end, comp,
                             __gnu_parallel::default_parallel_tag(numThreads_));
        break;
      case SortMode::PERM_STD_PAR:
      case SortMode::ROWP_STD_PAR:
      case SortMode::ROWTABLE_STD_PAR: {
        // libstdc++'s parallel algorithms run on TBB (if its headers are
        // found), which has no per-call thread count, only this global limit
        // that holds while `limit` is alive.
        tbb::global_control limit{
            tbb::global_control::max_allowed_parallelism, numThreads_};
        std::sort(std::execution::par, begin, end, comp);
        break;
      }
      case SortMode::PERM_QL_BIS:
      case SortMode::ROWP_QL_BIS:
      case SortMode::ROWTABLE_QL_BIS:
        ad_utility::blockSort::blockIndirectSort(
            ql::ranges::subrange(begin, end), comp, numThreads_,
            blockSortPool(numThreads_).get_executor());
        break;
      default:
        throw std::runtime_error("no valid mode selected for Sorter");
    }
  }
};

// wrapper for boost sort
struct BoostSorter {
  uint32_t numThreads_;

  template <typename It, typename Comp>
  void operator()(It begin, It end, Comp comp) const {
    boost::sort::block_indirect_sort(begin, end, comp, numThreads_);
  }
};

}  // namespace detail

template <int WIDTH>
IdTableStatic<WIDTH> copyWithAppliedPermutation(IdTableStatic<WIDTH>& stab,
  std::vector<std::size_t>& perm, uint32_t numThreads) {
  IdTableStatic<WIDTH> result{stab.numColumns(), stab.getAllocator()};
  result.resize(stab.numRows());

  for (size_t col = 0; col < stab.numColumns(); ++col) {
    auto src = stab.getColumn(col);
    auto dst = result.getColumn(col);
    const size_t numRows = stab.numRows();
#pragma omp parallel for num_threads(numThreads)
    for (size_t i = 0; i < numRows; ++i) {
      dst[i] = src[perm[i]];
    }
  }
  return result;
}

template <int WIDTH, typename Sorter = detail::Sorter>
void sortByPermutation(IdTable* table, const std::vector<ColumnIndex>& sortCols,
        Sorter sorter, uint32_t numThreads, std::string_view label = "") {
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

  auto sortStart = std::chrono::steady_clock::now();
  sorter(perm.begin(), perm.end(), comparison);
  auto sortEnd = std::chrono::steady_clock::now();

  *table = std::move(copyWithAppliedPermutation<WIDTH>(stab, perm, numThreads)).toDynamic();
  auto copyEnd = std::chrono::steady_clock::now();
  std::cerr << "[timing] " << label << " rows=" << numRows
            << " sort_ms=" << std::chrono::duration<double, std::milli>(
                                  sortEnd - sortStart).count()
            << " copy_ms=" << std::chrono::duration<double, std::milli>(
                                  copyEnd - sortEnd).count() << "\n";
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
