#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <random>
#include <vector>

#include "benchmark.h"
#include "ips4o.hpp"


// Benchmark options
constexpr std::array<int, 3> amount_rel_columns = {1, 2, 3};
constexpr std::size_t col_counts[] = {1, 2, 3, 4, 5};
const std::vector<std::size_t> row_counts = {1'000, 10'000, 100'000, 1'000'000};
const int trials_per_config = 3;


// ---------------------------------------------------------------------
// Runs benchmark_sort<sort_column_amount, column_sizes>(...) for each Ns in the explicit list of
// column counts, printing one CSV row per call.
// ---------------------------------------------------------------------
template <int sort_column_amount, std::size_t... column_amounts>
void run_benchmarks(std::size_t num_rows, int trials) {
    auto run_one = [&](auto sca_const, auto ncols_const) {
        // unwrap constexprs
        constexpr std::size_t columns = decltype(ncols_const)::value;
        constexpr int sca = decltype(sca_const)::value;

        // early out if table has less columns than necessary for sorting benchmark
        if constexpr (columns >= sca) {
            for (int t = 0; t < trials; ++t) {
                double ms = benchmark_sort<columns, sca>(num_rows);
                std::cout << columns << "," << num_rows << "," << sca <<"," << t << "," << ms << "\n";
            }
        }
    }; // run_one
        (run_one(std::integral_constant<int, sort_column_amount>{},
                 std::integral_constant<std::size_t, column_amounts>{}), ...);
}

// unpack col_counts
template <std::size_t I, std::size_t... Js>
void run_one(std::size_t rows, int trials, std::index_sequence<Js...>) {
    run_benchmarks<amount_rel_columns[I], col_counts[Js]...>(rows, trials);
}


// unpack the amount of relevant columns
template <std::size_t... Is>
void run_all(std::size_t rows, int trials, std::index_sequence<Is...>) {
    (run_one<Is>(rows, trials, std::make_index_sequence<std::size(col_counts)>{}), ...);
}

// ---------------------------------------------------------------------
// Core benchmark: build a table with N columns and `num_rows` rows,
// fill with random ints, time a single std::sort call whose comparator uses
// variable amount of columns
// ---------------------------------------------------------------------
template <std::size_t N, int sort_column_amount>
double benchmark_sort(std::size_t num_rows) {

    Table<N> table = make_table<N>(num_rows);

    auto start = std::chrono::steady_clock::now();
    #if defined(COL_MODE_PERM_SORT)
        table.template sort_by_permutation<sort_column_amount>();
    #elif defined(IPS4O)
        ips4o::parallel::sort(table.begin(), table.end(),
                Table<N>::template make_cmp<sort_column_amount>());
    #else
        std::sort(table.begin(), table.end(),
                Table<N>::template make_cmp<sort_column_amount>());
    #endif
    
    auto end = std::chrono::steady_clock::now();


    return std::chrono::duration<double, std::milli>(end - start).count();
}

// make table in different function so it's distinct in flamegraph
template <std::size_t N>
Table<N> make_table(std:: size_t num_rows) {
    std::mt19937 rng(42); // fixed seed for reproducibility across runs
    Table<N> table(num_rows);
    table.fill(rng);
    return table;
}



int main() {
    // csv header
    std::cout << "num_columns,num_rows,sort_column_amount,trial,time_ms\n";
    
    for (std::size_t rows : row_counts) {
        run_all(rows, trials_per_config,
             std::make_index_sequence<amount_rel_columns.size()>{});
    }

    return 0;
}
