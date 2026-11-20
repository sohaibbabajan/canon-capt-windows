// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.hpp"
#include <filesystem>

namespace capt::win {
inline constexpr wchar_t queue_name[] = L"Canon LBP6000 CAPT";
inline constexpr wchar_t queue_marker[] = L"Independent Canon CAPT for Windows ARM64";
inline constexpr wchar_t port_name[] = L"CanonCaptLoopbackLpr";
inline constexpr wchar_t service_name[] = L"CanonCaptQueue";
inline constexpr wchar_t service_account[] = L"NT AUTHORITY\\LocalService";
inline constexpr wchar_t host_name[] = L"127.0.0.1";
inline constexpr unsigned short lpr_port = 515;
inline constexpr wchar_t worker_name[] = L"CAPT-Queue.exe";
inline constexpr wchar_t pdfium_name[] = L"pdfium.dll";
inline constexpr wchar_t state_dir_name[] = L"state";
inline constexpr wchar_t jobs_dir_name[] = L"jobs";
inline constexpr wchar_t report_name[] = L"Last-job.txt";
inline constexpr DWORD cancel_job_control = 128;

int run_queue_service();
int print_file(HANDLE file, std::ostream& log, ULONGLONG timeout_ms, HANDLE stop = nullptr, HANDLE cancel = nullptr);
void validate_pdf_file(HANDLE file);
}
