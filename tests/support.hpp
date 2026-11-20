// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdexcept>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

template<class Exception, class Function>
void expect_throw(Function function)
{
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown");
}
