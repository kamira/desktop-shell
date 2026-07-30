// content/widgets/c2_08/note_widget.cpp — C2-08 筆記 widget 實作（平台中立、純邏輯組裝）。
#include "note_widget.hpp"

#include <stdexcept>
#include <utility>

namespace ds::widgets {

const char* to_string(NoteStatus s) noexcept {
    switch (s) {
        case NoteStatus::Ok:
            return "note.ok";
        case NoteStatus::Invalid:
            return "note.invalid";
    }
    return "note.invalid";
}

NoteWidget::NoteWidget(std::string id, ds::kernel::KernelBackend& backend,
                       ds::kernel::LayerStack& layers, const ds::render::FontMetrics& metrics,
                       ds::render::LayoutConstraints constraints)
    : id_(std::move(id)),
      base_(id_, backend, layers),
      content_(metrics, std::move(constraints)) {}

// --- 基底（C1-01）：全數透傳 SkinProfile ---

ds::profiles::SkinStatus NoteWidget::load_base(const ds::format::Value& definition) {
    return base_.load_skin(definition);
}

bool NoteWidget::unload_base() { return base_.unload(); }

bool NoteWidget::is_base_loaded() const { return base_.is_loaded(); }

ds::profiles::SkinState NoteWidget::base_state() const { return base_.state(); }

ds::kernel::DragStatus NoteWidget::place(const ds::kernel::AnchorSpec& spec) {
    return base_.place(spec);
}

ds::kernel::DragStatus NoteWidget::begin_drag() { return base_.begin_drag(); }

ds::kernel::DragStatus NoteWidget::drag_to(const ds::kernel::AnchorSpec& spec) {
    return base_.drag_to(spec);
}

ds::kernel::DragStatus NoteWidget::end_drag() { return base_.end_drag(); }

ds::kernel::DragStatus NoteWidget::cancel_drag() { return base_.cancel_drag(); }

bool NoteWidget::is_dragging() const { return base_.is_dragging(); }

// --- 編輯（E4-15）：全數透傳 TextInputElement ---

void NoteWidget::set_text(const std::string& utf8_text) { content_.set_text(utf8_text); }

void NoteWidget::insert(const std::string& utf8_text) { content_.insert(utf8_text); }

void NoteWidget::backspace() { content_.backspace(); }

void NoteWidget::erase_forward() { content_.erase_forward(); }

std::string NoteWidget::text() const { return content_.text(); }

std::size_t NoteWidget::length() const noexcept { return content_.length(); }

// --- 內容持久化（E7-12，經 E7-01）---

NoteStatus NoteWidget::load(const std::string& serialized_text) {
    const ds::format::ParseResult result = ds::format::parse(serialized_text);
    if (!result.ok()) {
        return NoteStatus::Invalid;  // E7-01 語法錯誤 / 版本不相容：內容不變。
    }

    const ds::format::Value& root = result.document().root;  // 契約上恆為 Map（E7-01 保證）。
    const ds::format::Value* field = root.find("text");

    std::string new_text;  // 缺席 → 視為空筆記（保留預設空字串）。
    if (field != nullptr) {
        if (!field->is_string()) {
            return NoteStatus::Invalid;  // text 欄位型別非字串：內容不變。
        }
        new_text = field->as_string();
    }

    try {
        content_.set_text(new_text);  // 非法 UTF-8（罕見：手工/損壞的序列化文字）→ 擲例外，於此攔截。
    } catch (const std::invalid_argument&) {
        return NoteStatus::Invalid;  // 轉譯為具名結果，不靜默、不變動既有內容。
    }

    return NoteStatus::Ok;
}

std::string NoteWidget::save() const {
    ds::format::Value root =
        ds::format::Value::map({{"text", ds::format::Value::string(content_.text())}});
    return ds::format::serialize(root, ds::format::kSupportedFormat);
}

// --- 顯示（E4-01，經 E4-15）---

ds::elements::TextInputRenderModel NoteWidget::render_model() const {
    return content_.render_model();
}

}  // namespace ds::widgets
