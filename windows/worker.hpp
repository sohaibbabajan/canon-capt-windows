// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common.hpp"

namespace capt::win {
// Owns a child in a kill-on-close job. Parent never performs USB I/O or waits
// synchronously for the child. All access from one supervising thread.
class Worker {
public:
    void start(const std::wstring& executable, const std::wstring& mode, HANDLE document = nullptr,
               HANDLE cancel = nullptr);
    std::string read_available();
    bool exited() const;
    DWORD exit_code() const;
    void stop();
    bool active() const { return static_cast<bool>(process_); }
    void clear(); // closes job first, ensuring no unsupervised child survives
private:
    Handle pipe_, process_, job_;
};
std::wstring executable_path();
} // namespace capt::win
