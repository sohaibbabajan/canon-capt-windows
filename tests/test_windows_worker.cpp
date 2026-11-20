// SPDX-License-Identifier: GPL-3.0-or-later
#include "worker.hpp"
#include "support.hpp"
#include <iostream>

namespace {
std::string await_exit(capt::win::Worker& worker)
{
    const auto deadline = GetTickCount64() + 5000;
    std::string output;
    while (GetTickCount64() < deadline) {
        const auto bytes = worker.read_available();
        output += bytes;
        CHECK(output.size() < 4096);
        if (worker.exited() && bytes.empty()) return output;
        Sleep(10);
    }
    worker.stop();
    throw std::runtime_error("Worker did not exit in 5 seconds");
}
}
int wmain(int argc, wchar_t** argv)
{
    try {
        CHECK(argc == 2);
        capt::win::Worker worker;
        expect_throw<std::invalid_argument>([&] { worker.start(argv[1], L"--arbitrary"); });
        CHECK(!worker.active());
        for (int repeat = 0; repeat < 3; ++repeat) {
            worker.start(argv[1], L"--self-test");
            expect_throw<std::logic_error>([&] { worker.start(argv[1], L"--self-test"); });
            CHECK(await_exit(worker) == "Worker launch and report pipe passed. No USB access.\r\n");
            CHECK(worker.exit_code() == 0);
            worker.clear();
            CHECK(!worker.active());
        }
        worker.start(argv[1], L"--watchdog-test");
        Sleep(100);
        CHECK(!worker.exited());
        expect_throw<std::logic_error>([&] { worker.exit_code(); });
        worker.stop();
        CHECK(await_exit(worker).empty());
        CHECK(worker.exit_code() == ERROR_CANCELLED);
        worker.clear();
        expect_throw<std::exception>([&] { worker.start(L"Z:\\missing-capt-test-directory\\missing.exe", L"--self-test"); });
        CHECK(!worker.active());
        std::cout << "Windows worker launch, exclusive lifecycle, pipe and termination passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
