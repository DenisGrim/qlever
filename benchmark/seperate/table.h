#include <array>
#include <algorithm>
#include <numeric>
#include <random>

#if defined(IPS4O)
#include "ips4o.hpp"
#endif

#ifndef TABLE_H
#define TABLE_H

template <int N>
struct RowTable {
private:
    using Table = std::vector<std::array<int, N>>;
    Table rows;

public:
    RowTable() = default;

    explicit RowTable(size_t n) : rows(n) {}

    template<int amount_sorting_columns>
    static auto make_cmp() {
        return [](const std::array<int, N>& a, const std::array<int, N>& b) {
            int sa{}, sb{};
            for (int i = 0; i < amount_sorting_columns; ++i) { sa += a[i]; sb += b[i]; }
            return sa < sb;
        };
    }
    void fill(std::mt19937& rng) {
        std::uniform_int_distribution<int> dist(0, 1'000'000);
        for (auto& field : rows) {
            for (auto& val : field) val = dist(rng);
        }
    }

    auto begin() {return rows.begin();}
    auto end() {return rows.end();}
}; // RowTable



template <int N>
struct ColumnTable {
private:
    using Table = std::array<std::vector<int>, N>;
    Table columns;

public:
    ColumnTable() = default;

    explicit ColumnTable(size_t n) {
        for (auto& col : columns) {col.resize(n);};
    }

#if defined(COL_MODE_NORMAL_SORT)
    struct RowProxy {
        Table* table;
        std::ptrdiff_t idx;

        // define index so it can be used the same way as std::array in make_sum
        int operator[](int c) const { return (*table)[c][idx]; }

        // conversion std::array<std:: vecotr<int>, N>
        operator std::array<int, N>() const {
            std::array<int, N> row;
            for (int c = 0; c < N; ++c) row[c] = (*table)[c][idx];
            return row;
        }

        // custom comparator and swap for std::sort to use
        RowProxy& operator=(const std::array<int, N>& row) {
            for (int c = 0; c < N; ++c) (*table)[c][idx] = row[c];
            return *this;
        }
        RowProxy& operator=(const RowProxy& other) {
            for (int c = 0; c < N; ++c) (*table)[c][idx] = (*other.table)[c][other.idx];
            return *this;
        }
        friend void swap(RowProxy a, RowProxy b) {
            for (int c = 0; c < N; ++c)
                std::swap((*a.table)[c][a.idx], (*b.table)[c][b.idx]);
        }
    };

    struct RowIterator {
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = std::array<int, N>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void;
        using reference         = RowProxy;

        Table* table;
        std::ptrdiff_t idx;

        reference operator*() const { return {table, idx}; }
        reference operator[](difference_type n) const { return {table, idx + n}; }

        RowIterator& operator++() { ++idx; return *this; }
        RowIterator operator++(int) { auto tmp = *this; ++idx; return tmp; }
        RowIterator& operator--() { --idx; return *this; }
        RowIterator operator--(int) { auto tmp = *this; --idx; return tmp; }

        RowIterator& operator+=(difference_type n) { idx += n; return *this; }
        RowIterator& operator-=(difference_type n) { idx -= n; return *this; }
        RowIterator operator+(difference_type n) const { return {table, idx + n}; }
        RowIterator operator-(difference_type n) const { return {table, idx - n}; }
        difference_type operator-(const RowIterator& o) const { return idx - o.idx; }

        bool operator==(const RowIterator& o) const { return idx == o.idx; }
        bool operator!=(const RowIterator& o) const { return idx != o.idx; }
        bool operator<(const RowIterator& o)  const { return idx < o.idx; }
        bool operator>(const RowIterator& o)  const { return idx > o.idx; }
        bool operator<=(const RowIterator& o) const { return idx <= o.idx; }
        bool operator>=(const RowIterator& o) const { return idx >= o.idx; }
    };

    RowIterator begin() { return {&columns, 0}; }
    RowIterator end()   { return {&columns, (std::ptrdiff_t)columns[0].size()}; }
    // typename makes sure it can be used no matter if std::sort puts in RowProxy or std::array
    template<typename Row>
    static int row_sum(const Row& row, int amount) {
        int s = 0;
        for (int c = 0; c < amount; ++c) s += row[c];
        return s;
    }

    // asc = amount_sorting_columns to be used in sum
    template<int asc>
    static auto make_cmp() {
        static_assert(asc <= N, "more sorting columns than columns available!");
        return [](const auto& a, const auto& b) { return row_sum(a, asc) < row_sum(b, asc); };
    }

#endif // NO PERM_SORT

    void fill(std::mt19937& rng) {
        std::uniform_int_distribution<int> dist(0, 1'000'000);
        for (auto& field : columns) {
            for (auto& val : field) val = dist(rng);
        }
    }

    // asc, amount_sorting_columns
    template <int asc>
    void sort_by_permutation() {
        static_assert(asc <= N, "more sorting columns than columns available!");
        std::size_t num_rows = columns[0].size();

        // identity permutation
        std::vector<std::size_t> perm(num_rows);
        std::iota(perm.begin(), perm.end(), 0);

        // sort permutation, comparator only reads `asc` columns
#if defined(IPS4O)
        ips4o::parallel::sort
#else
        std::sort
#endif
        (perm.begin(), perm.end(), [this](std::size_t i, std::size_t j) {
            int sum_a = 0, sum_b = 0;
            for (int c = 0; c < asc; ++c) { sum_a += columns[c][i]; sum_b += columns[c][j]; }
            return sum_a < sum_b;
        });

        // apply the permutation to entire table
        for (auto& col : columns) {
            std::vector<int> sorted_col(num_rows);
            for (std::size_t i = 0; i < num_rows; ++i) sorted_col[i] = col[perm[i]];
            col = std::move(sorted_col);
        }
    }

}; //ColumnTable

#endif // TABLE_H
