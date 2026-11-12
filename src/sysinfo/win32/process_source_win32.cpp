// W1-08 其二：真實 win32 行程列舉（實作）
//
// 本檔是本單元唯一無法在非 Windows 上建置的部分，故刻意只留「取讀數」這一件事：
// 所有換算與政策判斷都在 process_policy.cpp（平台中立、進得了 ubuntu 閘門）。
#include "process_source_win32.hpp"

#include <utility>
#include <vector>

namespace ds::sysinfo::win32 {
namespace {

// FILETIME 是 100ns 刻的 64 位元計數，與 GetProcessTimes 同單位 ——
// 故取樣時刻與行程 CPU 時間可以直接相減，中間不經任何浮點換算。
Ticks100ns ticks_of(const FILETIME& ft) noexcept {
    ULARGE_INTEGER v{};
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return static_cast<Ticks100ns>(v.QuadPart);
}

// EnumProcesses 的緩衝上限（4096 * 4 bytes = 16 KiB 的 pid 空間，約 4096 個行程）。
// 不讓它無限成長：列舉緩衝需求異常暴增時寧可回報列舉失敗，也不要在取樣路徑上吃掉記憶體。
constexpr std::size_t kMaxPidSlots = 4096;

// 讀取單一 pid 的五欄位。任一欄位讀不到就回 false ——
// **回報一筆「有些欄位是猜的」紀錄比不回報更糟**（K-002）。呼叫端把 false 計入 skipped。
bool read_entry(DWORD pid, RawProcessEntry& out) {
    // PROCESS_QUERY_LIMITED_INFORMATION：Vista 起可用於**受保護程度較高**的行程，
    // 且不需要 PROCESS_VM_READ。失敗是常態（Idle(0) / System(4) / 受保護行程 / 剛結束）。
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) {
        return false;
    }

    bool ok = false;
    do {
        FILETIME creation{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(h, &creation, &exited, &kernel, &user)) {
            break;
        }

        // 映像檔完整路徑 → 基本名。QueryFullProcessImageNameW 只需 QUERY_LIMITED_INFORMATION
        // （GetModuleBaseNameW 還要 PROCESS_VM_READ，在受保護行程上失敗率高得多）。
        wchar_t path[MAX_PATH * 2];
        DWORD path_len = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
        if (!QueryFullProcessImageNameW(h, 0, path, &path_len) || path_len == 0) {
            break;
        }
        std::string name = basename_of(utf8_from_wide(path, path_len));
        if (name.empty()) {
            break;
        }

        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        bool mem_ok = GetProcessMemoryInfo(h, &pmc, sizeof(pmc)) != FALSE;
        if (!mem_ok) {
            // 舊版行為上 PROCESS_MEMORY_COUNTERS 需要 PROCESS_VM_READ。只在第一次失敗後
            // 才要求這個較強的權限：一開始就要它會讓受保護行程的失敗率大幅上升。
            HANDLE h_vm =
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (h_vm != nullptr) {
                mem_ok = GetProcessMemoryInfo(h_vm, &pmc, sizeof(pmc)) != FALSE;
                CloseHandle(h_vm);
            }
        }
        if (!mem_ok) {
            break;
        }

        out.pid = static_cast<std::uint64_t>(pid);
        out.name = std::move(name);
        out.busy_100ns = ticks_of(kernel) + ticks_of(user);
        out.created_100ns = ticks_of(creation);
        out.memory_bytes = static_cast<std::uint64_t>(pmc.WorkingSetSize);

        // 已結束待回收 → Zombie。讀不到就誠實回 Unknown（三態，見 RawLiveness）。
        //
        // 已知限制：行程若以 259（STILL_ACTIVE / STATUS_PENDING）為結束碼，會被判為仍在
        // 執行。這是 GetExitCodeProcess 本身的歧義，無法在此分辨；影響僅限 status 欄位，
        // 不影響 pid / 名稱 / CPU% / 記憶體。
        DWORD exit_code = 0;
        if (GetExitCodeProcess(h, &exit_code)) {
            out.liveness =
                (exit_code == STILL_ACTIVE) ? RawLiveness::Running : RawLiveness::Exited;
        } else {
            out.liveness = RawLiveness::Unknown;
        }

        ok = true;
    } while (false);

    CloseHandle(h);
    return ok;
}

}  // namespace

std::string utf8_from_wide(const wchar_t* text, std::size_t length) {
    if (text == nullptr || length == 0) {
        return std::string();
    }
    const int in_len = static_cast<int>(length);
    const int need = WideCharToMultiByte(CP_UTF8, 0, text, in_len, nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(need), '\0');
    const int written =
        WideCharToMultiByte(CP_UTF8, 0, text, in_len, out.data(), need, nullptr, nullptr);
    if (written <= 0) {
        return std::string();
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

std::string basename_of(const std::string& path) {
    const std::size_t cut = path.find_last_of("\\/");
    if (cut == std::string::npos) {
        return path;
    }
    return path.substr(cut + 1);
}

RawSnapshot Win32ProcessSource::capture_raw() const {
    RawSnapshot raw;

    // 取樣時刻與行程 CPU 時間同單位（100ns 刻）。
    // 已知限制：系統時鐘被調整會讓這一個區間失真 —— 往後跳時 wall_delta 為 0，
    // 政策層回報 NoTimeElapsed（無讀值，fail-closed）；往前跳只會低估，仍在值域內。
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    raw.wall_100ns = ticks_of(now);

    // ALL_PROCESSOR_GROUPS：>64 核的機器上 GetSystemInfo().dwNumberOfProcessors 只看得到
    // 本行程所屬的處理器群組，會把 CPU% 的除數算小、使用率算高。失敗回 0 → 政策層判無讀值。
    raw.processor_count = static_cast<std::uint32_t>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

    std::vector<DWORD> pids(256);
    DWORD needed = 0;
    for (;;) {
        const DWORD cb = static_cast<DWORD>(pids.size() * sizeof(DWORD));
        if (!EnumProcesses(pids.data(), cb, &needed)) {
            // enumerated 維持 false → 政策層回 unknown（列舉失敗 ≠ 沒有行程）。
            return raw;
        }
        // needed == cb 時無從得知是否被截斷（API 不回報截斷），故一律當成要擴充。
        if (needed < cb) {
            break;
        }
        if (pids.size() >= kMaxPidSlots) {
            return raw;  // 同上：寧可誠實回報列舉失敗
        }
        pids.resize(pids.size() * 2);
    }

    raw.enumerated = true;
    const std::size_t count = needed / sizeof(DWORD);
    raw.listed_pids = count;
    raw.entries.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        RawProcessEntry entry;
        if (!read_entry(pids[i], entry)) {
            ++raw.skipped;  // 常態：受保護行程 / 剛結束的 pid
            continue;
        }
        raw.entries.push_back(std::move(entry));
    }

    return raw;
}

ProcessSample Win32ProcessSource::sample() {
    return tracker_.accept(capture_raw());
}

}  // namespace ds::sysinfo::win32
