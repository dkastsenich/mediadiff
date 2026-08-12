#pragma once

// mediadiff::expected<T, E> — the ONLY file in this repository permitted to
// name the backing library type (tl::expected), per locked decision D-02.
//
// No other translation unit may write `tl::expected` or `tl::unexpected`
// directly. This indirection is the load-bearing part of D-02, not the
// library choice itself: a future move to std::expected (available once the
// project can move past C++20 to C++23) becomes a one-header change instead
// of a project-wide rewrite.
//
// This header intentionally re-implements nothing — and(_then)/transform/
// or_else and friends are exactly the "small but easy to get subtly wrong"
// monadic surface D-02 explicitly avoids hand-rolling.

#include <tl/expected.hpp>

namespace mediadiff {

template <typename T, typename E>
using expected = tl::expected<T, E>;

template <typename E>
using unexpected = tl::unexpected<E>;

}  // namespace mediadiff
