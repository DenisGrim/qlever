#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <vector>

#include "table.h"

#ifndef BENCHMARK_H
#define BENCHMARK_H


// decide what table type
#if defined(ROW_MODE)
    template <int N> using Table = RowTable<N>;
#elif defined(COL_MODE_NORMAL_SORT)
    template <int N> using Table = ColumnTable<N>;
#elif defined(COL_MODE_PERM_SORT)
    template <int N> using Table = ColumnTable<N>;
#else
    #error "compile with -DROW_MODE / -DCOL_MODE_NORMAL_SORT / -DCOL_MODE_PERM_SORT"
#endif


template <int sort_column_amount, std::size_t... Ns>
void run_benchmarks(std::size_t num_rows, int trials);

template <int N>
auto make_cmp();

template <std::size_t... Is>
void run_all(std::size_t rows, int trials, std::index_sequence<Is...>);

template <std::size_t N, int sort_column_amount>
double benchmark_sort(std::size_t num_rows);

template <std::size_t N>
Table<N> make_table(std::size_t num_rows);

#endif // BENCHMARK_H
