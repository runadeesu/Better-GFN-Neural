#include "ui/I18n.h"

#include <string_view>
#include <unordered_map>

#ifdef _WIN32
#include "platform/Win32.h"
#endif

namespace bgn {

namespace {
UiLanguage gLang = UiLanguage::English;

const std::unordered_map<std::string_view, const char*>& japanese() {
    static const std::unordered_map<std::string_view, const char*> t = {
        // Navigation
        {"Home", "ホーム"},
        {"Enhancement", "画質強化"},
        {"Display", "ディスプレイ"},
        {"Games", "ゲーム"},
        {"Performance", "パフォーマンス"},
        {"Benchmark", "ベンチマーク"},
        {"Settings", "設定"},
        // Status
        {"Waiting", "待機中"},
        {"Connected", "接続済み"},
        {"Enhancing", "強化中"},
        {"Paused", "一時停止"},
        {"GeForce NOW is not running", "GeForce NOW が起動していません"},
        {"GeForce NOW is running. Start a game to enhance it automatically.", "GeForce NOW を検出しました。ゲームを開始すると自動で画質強化が始まります。"},
        {"Launch GeForce NOW", "GeForce NOWを起動"},
        {"Waiting for GeForce NOW", "GeForce NOW を待機中"},
        {"No hardware GPU", "ハードウェア GPU なし"},
        {"Enhancing", "強化中"},
        {"Ready - switch to GeForce NOW to see the enhanced picture", "準備完了 - GeForce NOW に切り替えると強化映像が表示されます"},
        {"Pause enhancement", "画質強化を一時停止"},
        {"Resume enhancement", "画質強化を再開"},
        {"Start enhancement", "画質強化を開始"},
        {"GFN STATUS", "GFN ステータス"},
        {"CURRENT GAME", "現在のゲーム"},
        {"ENHANCEMENT STATUS", "画質強化"},
        {"AUTO MODE", "オートモード"},
        {"OUTPUT RESOLUTION", "出力解像度"},
        {"OUTPUT FPS", "出力 FPS"},
        {"LATENCY", "遅延"},
        {"GPU", "GPU"},
        {"Not running", "未起動"},
        {"No game", "ゲームなし"},
        {"Active", "動作中"},
        {"Standby", "スタンバイ"},
        {"Off", "オフ"},
        {"On", "オン"},
        {"Auto", "自動"},
        {"added", "追加"},
        {"Quick settings", "クイック設定"},
        {"Preset", "プリセット"},
        {"Ultra", "ウルトラ"},
        {"Quality", "クオリティ"},
        {"Balanced", "バランス"},
        {"Performance", "パフォーマンス"},
        {"Low Latency", "低遅延"},
        {"Auto Mode", "オートモード"},
        {"Low Latency Mode", "低遅延モード"},
        {"Frame Interpolation", "フレーム補間"},
        {"Advanced settings", "詳細設定"},
        {"Live performance", "リアルタイム性能"},
        // Enhancement page
        {"Neural Super Resolution", "Neural Super Resolution"},
        {"Upscaling mode", "アップスケールモード"},
        {"Native", "ネイティブ"},
        {"Stream resolution", "ストリーム解像度"},
        {"Stream Compression Cleanup", "ストリーム圧縮ノイズ除去"},
        {"Blocking / macroblock cleanup", "ブロックノイズ / マクロブロック除去"},
        {"Banding reduction", "バンディング低減"},
        {"Compression & mosquito noise reduction", "圧縮ノイズ / モスキートノイズ低減"},
        {"Temporal Reconstruction", "時間方向再構成"},
        {"Temporal stabilization & anti-flicker", "時間安定化 / ちらつき低減"},
        {"Deblur", "デブラー"},
        {"Adaptive deblur", "アダプティブデブラー"},
        {"Motion deblur (follows optical flow)", "モーションデブラー（オプティカルフロー連動）"},
        {"Adaptive Sharpening", "アダプティブシャープ"},
        {"Sharpness", "シャープネス"},
        {"Text & HUD clarity boost", "文字・HUD の鮮明化"},
        {"Protect skin tones", "肌の輪郭を保護"},
        {"HDR+ / Color", "HDR+ / カラー"},
        {"HDR+ mode", "HDR+ モード"},
        {"HDR+ intensity", "HDR+ 強度"},
        {"Color enhancement", "カラー強化"},
        {"Automatic color (scene adaptive)", "自動カラー（シーン適応）"},
        {"Black level", "黒レベル"},
        {"White level", "白レベル"},
        {"Contrast", "コントラスト"},
        {"Gamma", "ガンマ"},
        {"Saturation", "彩度"},
        {"Vibrance", "自然な彩度"},
        {"Color temperature", "色温度"},
        {"Highlight recovery", "ハイライト復元"},
        {"Shadow detail", "シャドウディテール"},
        {"Local contrast", "ローカルコントラスト"},
        {"Tone mapping", "トーンマッピング"},
        {"Reset to defaults", "初期値に戻す"},
        {"Strength", "強度"},
        {"Editing global settings", "グローバル設定を編集中"},
        // Display
        {"Output", "出力"},
        {"Output mode", "出力モード"},
        {"Match GFN window", "GFN ウィンドウに合わせる"},
        {"Fullscreen (upscale to monitor)", "フルスクリーン（モニターへアップスケール）"},
        {"Output resolution", "出力解像度"},
        {"Monitors", "モニター"},
        {"Preferred monitor", "優先モニター"},
        {"Automatic (monitor showing GeForce NOW)", "自動（GeForce NOW を表示中のモニター）"},
        {"Before / after split view", "比較表示（左: 元映像 / 右: 強化後）"},
        {"Capture method", "キャプチャ方式"},
        {"Primary", "プライマリ"},
        // Games
        {"Game profiles", "ゲームプロファイル"},
        {"Use global settings", "グローバル設定を使用"},
        {"Performance priority", "パフォーマンス優先度"},
        {"Delete profile", "プロファイルを削除"},
        {"sessions", "回プレイ"},
        {"Built-in", "プリセット"},
        {"No game profiles yet. Profiles are created automatically when a game is detected.", "まだプロファイルがありません。ゲームを検出すると自動的に作成されます。"},
        // Performance page
        {"Input FPS", "入力 FPS"},
        {"Output FPS", "出力 FPS"},
        {"Capture FPS", "キャプチャ FPS"},
        {"Input resolution", "入力解像度"},
        {"Processing resolution", "処理解像度"},
        {"Render time", "処理時間"},
        {"GPU usage", "GPU 使用率"},
        {"VRAM", "VRAM"},
        {"Frame time", "フレーム時間"},
        {"Upscale mode", "アップスケール方式"},
        {"Dropped frames", "ドロップフレーム"},
        {"Added latency", "追加処理遅延"},
        {"Quality tier", "品質ティア"},
        {"GPU time per stage", "ステージ別 GPU 時間"},
        {"Auto Mode decision", "オートモードの判断"},
        {"Pipeline", "パイプライン"},
        // Benchmark
        {"Run benchmark", "ベンチマークを実行"},
        {"Cancel", "キャンセル"},
        {"Average processing time", "平均処理時間"},
        {"Maximum processing time", "最大処理時間"},
        {"Recommended preset", "推奨プリセット"},
        {"Recommended output resolution", "推奨出力解像度"},
        {"Recommended frame interpolation", "推奨フレーム補間"},
        {"Apply recommendation", "推奨設定を適用"},
        {"No benchmark results yet.", "ベンチマーク結果はまだありません。"},
        // Settings
        {"Startup", "スタートアップ"},
        {"Start with Windows", "Windows 起動時に開始"},
        {"Start in the background (tray)", "バックグラウンド（タスクトレイ）で開始"},
        {"Close button minimizes to tray", "閉じるボタンでタスクトレイに格納"},
        {"Start enhancing automatically when a game starts", "ゲーム開始時に自動で画質強化を開始"},
        {"Show notifications", "通知を表示"},
        {"Language", "言語"},
        {"GeForce NOW detection", "GeForce NOW 検出"},
        {"Also detect GeForce NOW in web browsers", "Web ブラウザ版 GeForce NOW も検出"},
        {"GeForce NOW location", "GeForce NOW の場所"},
        {"Logs", "ログ"},
        {"Log level", "ログレベル"},
        {"Open logs folder", "ログフォルダを開く"},
        {"Controllers", "コントローラー"},
        {"No controllers detected", "コントローラーは検出されていません"},
        {"Reset all settings", "すべての設定をリセット"},
        {"About", "このアプリについて"},
        {"Quit Better GFN Neural", "Better GFN Neural を終了"},
        {"Safe mode is active after repeated abnormal shutdowns. Heavy features are limited.", "異常終了が続いたためセーフモードで起動しています。負荷の高い機能を制限しています。"},
        {"Leave safe mode", "セーフモードを解除"},
        // First run
        {"Welcome to Better GFN Neural", "Better GFN Neural へようこそ"},
        {"Checking GPU", "GPU を確認中"},
        {"Checking monitors", "モニターを確認中"},
        {"Looking for GeForce NOW", "GeForce NOW を検索中"},
        {"Enabling Auto Mode", "オートモードを設定中"},
        {"All set", "準備完了"},
        {"Get started", "はじめる"},
        {"Everything runs automatically. Start a game in GeForce NOW and it will be enhanced.", "すべて自動で動作します。GeForce NOW でゲームを開始すると画質強化が始まります。"},
        {"Unofficial companion app. Not affiliated with or endorsed by NVIDIA.", "非公式コンパニオンアプリです。NVIDIA とは関係がなく、承認も受けていません。"},
        {"Minimize", "最小化"},
        {"Close", "閉じる"},
        {"Open", "開く"},
        {"Exit", "終了"},
    };
    return t;
}
} // namespace

void setUiLanguage(UiLanguage lang) { gLang = lang; }
UiLanguage uiLanguage() { return gLang; }

UiLanguage resolveLanguage(const std::string& setting) {
    if (setting == "ja") return UiLanguage::Japanese;
    if (setting == "en") return UiLanguage::English;
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(id) == LANG_JAPANESE) return UiLanguage::Japanese;
#endif
    return UiLanguage::English;
}

const char* tr(const char* english) {
    if (gLang != UiLanguage::Japanese) return english;
    const auto& t = japanese();
    auto it = t.find(english);
    return it == t.end() ? english : it->second;
}

} // namespace bgn
