// H1-08 環形量表繪製 —— 消費 E4-03 `GaugeElement` 的弧輸出
//
// 本檔**不含任何 win32 呼叫、不 include windows.h、不含平台條件編譯**，其測試亦註冊在
// `if(WIN32)` 之外：ubuntu 的 G3（-DDS_WERROR=ON）、G3b（註冊數不得下降）與
// `gate_sanitizers`（ASan + UBSan）都真的建置並執行它。這不是風格選擇——
// `H1-05` 標著 `platform: neutral` 卻把整支 `add_test` 包在 `if(WIN32)` 內，於是它在
// ubuntu 的覆蓋**整個消失而三道防線全部放行**（CHG-20261112-07 的 G3b 註解已載明該實測）。
//
// ---------------------------------------------------------------------------
// 1. 與 H1-06 的關係：本單元是 kind 分派機制的第一個具體案例
// ---------------------------------------------------------------------------
// H1-06 內建的 `gauge` 畫法是「圓盤 + 一根指針」（`render_gauge`，只用得到
// `angle_degrees`）。本單元改畫**環形量表**：以 E4-03 的 `start_angle_degrees` /
// `sweep_degrees` 畫出整圈軌道弧，再以 `fill_ratio` 畫出其上的值弧。
// 接法只用 H1-06 的公開擴充點，`render_dispatch.*` / `widget_render.*` / `modules/` /
// `engine/` / `src/` **一行未改**：
//   - `install_ring_gauge()` → `replace_kind(kKindGauge, ...)`（明示覆寫內建畫法）
//     ＋ `register_kind(kKindRingGauge, ...)`（讓組版端可明確點名環形）。
//
// ---------------------------------------------------------------------------
// 2. 為什麼需要 `ArcSink` 這個額外介面
// ---------------------------------------------------------------------------
// H1-06 的 `PaintSink` 只有三個原語（填矩形 / 畫文字 / 畫指針），**沒有弧**。環形量表
// 無法以矩形拼出來（拼出來就是一串鋸齒，正好違反本單元「細線不得糊成一團」的驗收標準）。
//
// 故本檔新增 `ArcSink` —— 一個**獨立的、可選的**繪製介面，而**不是**往 `PaintSink`
// 加純虛擬函式：`PaintSink` 的既有實作（`tests/h1/test_h1_06.cpp` 與
// `tests/h1/test_h1_06_radial.cpp` 的 `RecordingSink`，皆為 `final`，且**不在本單元的
// 鎖定範圍內**）會當場編不過。兩個 sink 由呼叫端分別提供，誰都不必認識誰。
//
// ---------------------------------------------------------------------------
// 3. 裝置像素對齊（驗收標準：125% / 150% 下細線不得糊成一團）
// ---------------------------------------------------------------------------
// 輸入的 `PaintBox` 是**裝置像素**（GDI client 座標），而環的粗細是**邏輯像素**的設計值
// （`RingStyle`，隨 DPI 縮放）。若直接照 `粗細 × 1.25` 畫，一條 1px 的細線會變成 1.25px：
// 橫跨兩個像素、兩邊各淡一半，整圈看起來就是糊的。本檔的對齊規則：
//   - **筆寬**：`round(邏輯粗細 × scale)`，夾限為**整數**且**至少 1**（細線可以變粗、
//     不可以消失）。
//   - **圓心**：整數裝置像素座標。
//   - **內 / 外緣半徑**：整數裝置像素 → 環的兩條邊界剛好落在像素邊界上，像素要嘛整格被
//     環覆蓋、要嘛整格沒有，不存在「半格」。
// 筆刷型 API（GDI `Arc` / Direct2D `DrawArc`）要的是路徑半徑 + 筆寬，見
// `ArcSpan::mid_radius()` / `ArcSpan::stroke_width()`：筆寬為奇數時路徑半徑自然落在
// 半像素上，正是該類 API 畫出清晰筆畫所需的位置。反鋸齒模式本身屬 E4-24
// （`engine/render/render_style.hpp`，`AntiAliasMode`）的語彙，由實際的 `ArcSink`
// 實作去設定；本層負責的是**先把幾何對齊**，別讓反鋸齒去救一個本來就對不準的邊。
//
// ---------------------------------------------------------------------------
// 4. 指針掃向（H1-06 驗收時裁定「另案」的那一案，本單元是其落點）
// ---------------------------------------------------------------------------
// E4-03 `GaugeElement` 的預設弧是 start 135°、sweep **+270°**（數學慣例：0° 指向右、
// 逆時針為正）。於是 0% 在左上、50% 在**正下方**、100% 在右上——掃向從**下方**經過，
// 與一般量表（從下方左側起、經**上方**到下方右側）相反。
//
// **本層的決定：原封照 E4-03 的弧畫，繪製層不做任何方向轉換。** 理由有三：
//   1. 值→角度是 E4-03 **明文的語意輸出**（`bar_gauge.hpp` 檔頭：「量表的值→角度，
//      是量表本身的語意輸出」）。繪製層若偷偷反轉，同一份 `RenderModel` 在不同繪製層
//      會長成不同的東西，`angle_degrees`（元件算的）與環的值弧（繪製層算的）當場互相矛盾。
//   2. 弧**本來就可設定**：`GaugeElement::set_arc()`，以及 E4-03 設定文件的
//      `start_angle` / `sweep` 欄位。要常規掃向只要給 start 225°、sweep **-270°**
//      （負 sweep = 順時針，經上方），不必動任何程式碼。
//   3. 本層支援**負 sweep**（順時針）且有測試釘住，所以「另案」的修法在繪製層已經就緒，
//      缺的只是組版端選一組角度——而組版端（`host/app/**`、C2-02）不在本單元的鎖定範圍內。
// 現行預設弧造成的實際視覺位置另以測試釘住（`tests/h1/test_h1_08_ring_gauge.cpp` 的
// `SweepDirection` 一組），避免它日後又悄悄漂走。
#ifndef DS_HOST_PAINT_RING_GAUGE_HPP
#define DS_HOST_PAINT_RING_GAUGE_HPP

#include <cmath>

#include "render_dispatch.hpp"

namespace ds::host {

// 環形量表的具名 kind 鍵。**不是**內建 kind（內建三種見 `render_dispatch.hpp`）。
inline constexpr const char* kKindRingGauge = "ring_gauge";

// ---------------------------------------------------------------------------
// ArcSpan —— 一段環形弧（裝置像素）
// ---------------------------------------------------------------------------
// 座標慣例與 `PaintBox` 相同：左上原點、y 軸朝下。
// 角度慣例與 `PaintSink::draw_needle` 相同：度、0° 指向右、**逆時針為正**，
// y 軸朝下的翻轉由 `ArcSink` 的實作負責（GDI 側即 `cy - r*sin(θ)` 那條式子）。
struct ArcSpan {
    double center_x = 0.0;
    double center_y = 0.0;
    double outer_radius = 0.0;  // 外緣半徑（裝置像素，已對齊為整數）
    double inner_radius = 0.0;  // 內緣半徑（裝置像素，已對齊為整數）
    double start_degrees = 0.0;
    double sweep_degrees = 0.0;  // 正 = 逆時針，負 = 順時針；0 = 無弧

    // 筆寬（外緣 − 內緣）。筆刷型 API 用這個 + `mid_radius()`。
    double stroke_width() const noexcept { return outer_radius - inner_radius; }
    // 路徑半徑（環的中線）。筆寬為奇數時落在半像素上——筆刷型 API 要的正是這個位置。
    double mid_radius() const noexcept { return (outer_radius + inner_radius) * 0.5; }

    // 是否真的有東西可畫。
    //
    // **`std::isfinite(sweep_degrees)` 不可省**，這是本單元實際踩到的坑：
    // 大小比較（`> 0.0`）對 NaN 一律為 false，所以 `outer_radius > 0.0` 與
    // `stroke_width() > 0.0` 本身就是 fail-closed；但**不等於**（`!= 0.0`）剛好相反
    // ——NaN 與任何值都「不相等」，於是 `nan != 0.0` 為 **true**，一段 sweep 為 NaN 的弧
    // 會被判為「可見」而送進繪製 API。知識庫 K-002 那一族（壞掉的值偽裝成正常結果）的
    // 第三種寫法：把「正向比較即安全」套到 `!=` 上就會破功。
    // 寫下來的當下這裡就是錯的，是 `Degenerate.ArcSpanWithNanIsNotVisible` 抓出來的。
    bool visible() const noexcept {
        return std::isfinite(sweep_degrees) && sweep_degrees != 0.0 && outer_radius > 0.0 &&
               stroke_width() > 0.0;
    }
};

bool operator==(const ArcSpan& a, const ArcSpan& b) noexcept;
bool operator!=(const ArcSpan& a, const ArcSpan& b) noexcept;

// ---------------------------------------------------------------------------
// ArcSink —— 弧繪製原語（`PaintSink` 之外的可選介面，見檔頭 §2）
// ---------------------------------------------------------------------------
class ArcSink {
public:
    virtual ~ArcSink() = default;

    // 以具名角色的顏色畫一段環形弧。`span.visible()` 為 false 時本層不會呼叫。
    virtual void draw_arc(const ArcSpan& span, PaintRole role) = 0;

protected:
    ArcSink() = default;
    ArcSink(const ArcSink&) = default;
    ArcSink& operator=(const ArcSink&) = default;
};

// ---------------------------------------------------------------------------
// RingStyle —— 環的設計尺度，單位為**邏輯像素**（隨 DPI 縮放）
// ---------------------------------------------------------------------------
struct RingStyle {
    double thickness_logical = 6.0;  // 環的粗細
    double inset_logical = 2.0;      // 外緣相對格位的內縮（E4-24 的「內距」語彙）
};

// 本層的具名結果（不靜默：畫不出來一定要說得出是哪一種畫不出來）。
enum class RingStatus {
    Ok,             // 幾何成立，弧已產出
    EmptyArea,      // 格位無可畫面積（視窗被縮到極小）
    NoArc,          // 上游沒給角度（`ElementDraw::has_angle == false`）
    DegenerateArc,  // 有角度但弧退化：sweep 為 0 或非有限、或起始角非有限
};

const char* to_string(RingStatus status) noexcept;

// 一次環形量表的完整幾何（全部已對齊到裝置像素）。
struct RingGeometry {
    RingStatus status = RingStatus::EmptyArea;
    ArcSpan track{};             // 整段軌道弧（start, sweep）
    ArcSpan value{};             // 值弧（start, sweep × 夾限後的 fill_ratio）
    int stroke_device_px = 0;    // 筆寬（整數裝置像素，≥1）
    double dpi_scale = 1.0;      // 實際採用的縮放（已消毒）
    PaintBox bounds{};           // 環的外接正方形（裝置像素，邊界即外緣）
    PaintBox label_box{};        // 環內可安全放讀數的方框（內緣的內接正方形）；可能為空
};

// DPI 縮放消毒：非有限 / ≤0 → 1.0；過大 → 夾限（不崩潰、不產生垃圾座標）。
double sanitize_dpi_scale(double dpi_scale) noexcept;

// 邏輯粗細 × 縮放 → **整數**裝置像素筆寬，至少 1。
// 非有限 / ≤0 的輸入回 1 —— 細線可以變粗，不可以悄悄消失（K-002 族：壞掉的輸入不得
// 偽裝成一個「看起來正常」的結果，而「整條環不見了」比「畫成最細的一條」更難被發現）。
int device_stroke_px(double thickness_logical, double dpi_scale) noexcept;

// 由格位（裝置像素）+ 樣式 + DPI 縮放算出環形幾何。**純函式，不碰任何繪製 API。**
// 任何非法輸入（退化尺寸 / 退化弧 / NaN / 荒謬縮放）都走具名 `RingStatus`，絕不崩潰、
// 絕不產生未定義的 double→int 轉換（`-fsanitize=float-cast-overflow` 會把那種轉換
// 當場中止，不是印個訊息而已）。
RingGeometry compute_ring_geometry(const ElementDraw& draw, const PaintBox& area,
                                   const RingStyle& style, double dpi_scale);

// ---------------------------------------------------------------------------
// RingGaugePainter —— 把幾何變成繪製指令
// ---------------------------------------------------------------------------
// 統計數字的用途與 H1-06 的 `PaintSummary` 相同：讓「有沒有真的畫出來、有幾筆退化」
// 可被斷言，而不是只看畫面。
struct RingGaugeStats {
    int drawn = 0;           // 軌道 + 值弧都畫了
    int degraded = 0;        // NFR-03：該筆不可用 → 軌道仍畫、值弧不畫
    int empty_area = 0;      // 沒有可畫面積
    int no_arc = 0;          // 上游沒給角度 → 退回線性填充（值仍看得見）
    int degenerate_arc = 0;  // 弧退化 → 退回線性填充

    int total() const noexcept {
        return drawn + degraded + empty_area + no_arc + degenerate_arc;
    }
};

class RingGaugePainter {
public:
    // **生命週期（知識庫 K-013）**：`arcs` 必須活得比本物件久，本物件又必須活得比
    // 它被註冊進去的 `ElementRendererTable` 久 —— `make_ring_gauge_renderer()` 回傳的
    // `std::function` 以**指標**捕獲本物件（不是 const 參考綁臨時物件）。
    // 兩者皆由呼叫端持有為具名變數即可。
    explicit RingGaugePainter(ArcSink& arcs, RingStyle style = RingStyle{},
                              double dpi_scale = 1.0);

    const RingStyle& style() const noexcept { return style_; }
    void set_style(const RingStyle& style) noexcept { style_ = style; }

    // DPI 可在執行期變動（使用者把視窗拖到另一台螢幕）。設定值會先經消毒。
    double dpi_scale() const noexcept { return dpi_scale_; }
    void set_dpi_scale(double dpi_scale) noexcept;

    const RingGaugeStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = RingGaugeStats{}; }
    // 最近一次 `render()` 的結果（不靜默：呼叫端 / 測試都問得到）。
    RingStatus last_status() const noexcept { return last_status_; }

    // 畫一個環形量表。`area` 是 H1-06 分派給本 kind 的格位（裝置像素）。
    // 文字（中央讀數）走 `PaintSink`，弧走建構時給的 `ArcSink`。
    void render(const ElementDraw& draw, const PaintBox& area, PaintSink& sink);

private:
    ArcSink* arcs_;
    RingStyle style_;
    double dpi_scale_;
    RingGaugeStats stats_{};
    RingStatus last_status_ = RingStatus::EmptyArea;
};

// 把 painter 包成 H1-06 的 `ElementRenderer`。見 `RingGaugePainter` 的生命週期說明。
ElementRenderer make_ring_gauge_renderer(RingGaugePainter& painter);

// 把環形畫法接進分派表：
//   - `replace_kind(kKindGauge, ...)` —— **明示**覆寫 H1-06 內建的「圓盤 + 指針」畫法
//     （`replace_kind` 與 `register_kind` 分成兩個名字，就是要讓「我知道我在覆寫」
//     寫在呼叫端而非註解裡）；
//   - `register_kind(kKindRingGauge, ...)` —— 另給一個具名鍵，讓組版端可明確點名環形。
// 回傳 true 僅當兩者都成功（即首次安裝）。重複安裝時 `register_kind` 依其契約不靜默
// 覆寫而回 false，整體即回 false。
bool install_ring_gauge(ElementRendererTable& table, RingGaugePainter& painter);

}  // namespace ds::host

#endif  // DS_HOST_PAINT_RING_GAUGE_HPP
