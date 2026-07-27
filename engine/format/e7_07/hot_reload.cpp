// E7-07 熱重載 — 實作。見 hot_reload.hpp 檔首說明。平台中立、無 `#ifdef` / 真實檔案監看。
#include "hot_reload.hpp"

#include <stdexcept>
#include <utility>

namespace ds::format {

// -----------------------------------------------------------------------------
// MemorySource：相位 1 注入式來源（純記憶體，無檔案系統）
// -----------------------------------------------------------------------------

MemorySource::MemorySource(std::string initial) : content_(std::move(initial)) {}

std::string MemorySource::content() const { return content_; }

std::uint64_t MemorySource::revision() const { return revision_; }

void MemorySource::set_content(std::string next) {
    // 內容確實不同才視為變更並遞增 revision——否則相同寫入不應觸發重載。
    if (next == content_) {
        return;
    }
    content_ = std::move(next);
    ++revision_;
}

// -----------------------------------------------------------------------------
// HotReloader：生命週期狀態機
// -----------------------------------------------------------------------------

HotReloader::HotReloader(ReloadSource& source, FormatOptions options)
    : source_(source), options_(options) {}

const Document& HotReloader::document() const {
    if (!loaded_) {
        throw std::runtime_error("HotReloader::document(): 尚無有效 Document（從未成功載入）");
    }
    return document_;
}

const Value& HotReloader::root() const { return document().root; }

ReloadResult HotReloader::apply_load() {
    // 記下本次所依據的 revision，供後續 poll 的變更比較。
    seen_ = true;
    last_revision_ = source_.revision();

    ParseResult parsed = parse(source_.content());

    if (parsed.ok()) {
        // 成功：以新值替換當前有效狀態；清掉舊診斷；觸發成功回呼。
        document_ = parsed.document();
        loaded_ = true;
        last_diags_.clear();
        last_report_.clear();

        ReloadResult result;
        result.status = ReloadStatus::Loaded;
        result.has_document = true;
        if (on_reload_) {
            on_reload_(document_);
        }
        return result;
    }

    // 失敗：**保留舊值**（不覆寫 document_ / loaded_）；以 E7-06 產生定位到行的診斷。
    Diagnostic diag = Diagnostic::from_parse_error(parsed.error());
    last_diags_ = {diag};
    last_report_ = format_report(last_diags_, source_.content(), options_);

    ReloadResult result;
    result.status = ReloadStatus::Failed;
    result.has_document = loaded_;  // 若先前已有有效值則仍持有（保舊值）。
    result.diagnostics = last_diags_;
    result.report = last_report_;
    if (on_error_) {
        on_error_(last_diags_, last_report_);
    }
    return result;
}

ReloadResult HotReloader::poll() {
    // 從未載入 → 一律載入；否則僅在 revision 變更時載入。
    if (seen_ && source_.revision() == last_revision_) {
        ReloadResult result;
        result.status = ReloadStatus::Unchanged;
        result.has_document = loaded_;
        return result;
    }
    return apply_load();
}

ReloadResult HotReloader::reload() {
    // 強制重新解析，忽略 revision 比較（apply_load 內會同步 last_revision_）。
    return apply_load();
}

}  // namespace ds::format
