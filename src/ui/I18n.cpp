#include "ui/I18n.h"

#include <mutex>
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
        // Navigation / chrome
        {"Home", "ホーム"},
        {"Enhancement", "画質強化"},
        {"Display", "ディスプレイ"},
        {"Games", "ゲーム"},
        {"Performance", "パフォーマンス"},
        {"Benchmark", "ベンチマーク"},
        {"Settings", "設定"},
        {"Unofficial", "非公式"},
        {"Minimize", "最小化"},
        {"Close", "閉じる"},
        {"Open", "開く"},
        {"Exit", "終了"},
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
        {"Ready - switch to GeForce NOW to see the enhanced picture", "準備完了 - GeForce NOW に切り替えると強化映像が表示されます"},
        {"Pause enhancement", "画質強化を一時停止"},
        {"Resume enhancement", "画質強化を再開"},
        {"Start enhancement", "画質強化を開始"},
        {"SAFE MODE", "セーフモード"},
        {"GFN STATUS", "GFN ステータス"},
        {"CURRENT GAME", "現在のゲーム"},
        {"ENHANCEMENT STATUS", "画質強化"},
        {"AUTO MODE", "オートモード"},
        {"OUTPUT RESOLUTION", "出力解像度"},
        {"OUTPUT FPS", "出力 FPS"},
        {"LATENCY", "遅延"},
        {"Not running", "未起動"},
        {"No game", "ゲームなし"},
        {"Active", "動作中"},
        {"Standby", "スタンバイ"},
        {"Browser", "ブラウザ"},
        {"in {:.0f} fps{}", "入力 {:.0f} fps{}"},
        {"Off", "オフ"},
        {"On", "オン"},
        {"Auto", "自動"},
        {"added", "追加"},
        {"Quick settings", "クイック設定"},
        {"Preset", "プリセット"},
        {"Ultra", "ウルトラ"},
        {"Quality", "クオリティ"},
        {"Balanced", "バランス"},
        {"Low Latency", "低遅延"},
        {"Auto Mode", "オートモード"},
        {"Low Latency Mode", "低遅延モード"},
        {"Frame Interpolation", "フレーム補間"},
        {"Advanced settings", "詳細設定"},
        {"Live performance", "リアルタイム性能"},
        // Enhancement page
        {"Upscaling mode", "アップスケールモード"},
        {"Native", "ネイティブ"},
        {"Stream resolution", "ストリーム解像度"},
        {"Real-time CNN (trained in-house) reconstructs detail when the stream is lower resolution than your display. Not DLSS: GeForce NOW does not expose game motion vectors to local apps.",
         "本プロジェクトで学習したリアルタイム CNN が、ストリームがディスプレイより低解像度のときにディテールを再構成します。DLSS ではありません（GeForce NOW はゲームのモーションベクトルをローカルアプリに提供しないため）。"},
        {"Quality = Neural SR (L)   Balanced = Neural SR (S)   Performance = Lanczos-AR (non-neural)   Native = no upscaling",
         "クオリティ = Neural SR (L)　バランス = Neural SR (S)　パフォーマンス = Lanczos-AR（非ニューラル）　ネイティブ = アップスケールなし"},
        {"Stream Compression Cleanup", "ストリーム圧縮ノイズ除去"},
        {"Removes macroblocking, banding, mosquito noise and dark-scene artifacts from the video stream.",
         "映像ストリームのマクロブロック、バンディング、モスキートノイズ、暗いシーンのノイズを除去します。"},
        {"Blocking / macroblock cleanup", "ブロックノイズ / マクロブロック除去"},
        {"Smooths compression block edges in flat regions while keeping real edges.", "本来の輪郭は残したまま、平坦な部分の圧縮ブロックの境目を滑らかにします。"},
        {"Banding reduction", "バンディング低減"},
        {"Rebuilds smooth gradients (sky, fog, dark scenes) and dithers the output.", "空・霧・暗いシーンの滑らかなグラデーションを復元し、出力にディザをかけます。"},
        {"Compression & mosquito noise reduction", "圧縮ノイズ / モスキートノイズ低減"},
        {"Edge-aware denoise, stronger around edges where ringing appears and in dark scenes.",
         "輪郭を考慮したノイズ除去です。リンギングが出やすい輪郭の周りと暗いシーンで強めに働きます。"},
        {"Temporal Reconstruction", "時間方向再構成"},
        {"Stabilizes thin lines, text, foliage, fences and distant detail across frames using optical flow; rejects history on disocclusion to avoid ghosting.",
         "オプティカルフローで、細い線・文字・草木・フェンス・遠景をフレーム間で安定させます。隠れていた部分が見えたときは過去フレームを使わず、残像を防ぎます。"},
        {"Temporal stabilization & anti-flicker", "時間安定化 / ちらつき低減"},
        {"Deblur", "デブラー"},
        {"Recovers edges and texture lost to motion and stream softening. Strength adapts to motion speed automatically.",
         "動きやストリーム圧縮で失われた輪郭と質感を復元します。強さは動きの速さに合わせて自動で変わります。"},
        {"Adaptive deblur", "アダプティブデブラー"},
        {"Motion deblur (follows optical flow)", "モーションデブラー（オプティカルフロー連動）"},
        {"Adaptive Sharpening", "アダプティブシャープ"},
        {"Content-aware: more detail when still, less during fast motion, extra clarity for text/HUD, gentle on skin.",
         "内容に応じて自動調整：静止時はディテール強め、速い動きでは控えめ、文字/HUD はくっきり、肌はやさしく。"},
        {"Sharpness", "シャープネス"},
        {"Text & HUD clarity boost", "文字・HUD の鮮明化"},
        {"Protect skin tones", "肌の輪郭を保護"},
        {"Optical-flow frame interpolation (not neural). Auto enables it only when your display refresh rate allows extra frames and the added latency stays within budget. HUD pixels that do not move are never warped.",
         "オプティカルフローによるフレーム補間です（ニューラルではありません）。自動では、ディスプレイのリフレッシュレートに余裕があり、追加遅延が許容範囲に収まるときだけ有効になります。動かない HUD は変形させません。"},
        {"HDR+ / Color", "HDR+ / カラー"},
        {"HDR+ expands bright highlights on HDR displays; on SDR displays the same pipeline provides SDR Enhancement. Auto keeps saturation and black levels natural.",
         "HDR ディスプレイでは HDR+ が明るいハイライトを広げ、SDR ディスプレイでは同じ処理で SDR エンハンスを行います。自動では彩度と黒レベルを自然に保ちます。"},
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
        {"Monitors GPU time, VRAM, frame rates, refresh rate, GPU load and latency, and continuously picks the best quality tier. When the PC is too slow it lowers quality step by step instead of turning enhancement off.",
         "GPU 処理時間・VRAM・フレームレート・リフレッシュレート・GPU 負荷・遅延を監視し、最適な品質ティアを常に選びます。PC の性能が足りないときは強化を止めずに、品質を段階的に下げます。"},
        {"Minimal queueing (1 frame), immediate presentation, tighter latency budget for interpolation.",
         "フレームの待ち行列を最小（1 フレーム）にしてすぐに表示し、フレーム補間の遅延予算も厳しくします。"},
        {"When GeForce NOW fills the screen it scales the stream itself. Auto detects the real stream resolution and reconstructs it with Neural SR.",
         "GeForce NOW が全画面のときは GeForce NOW 自身が映像を拡大しています。自動では実際のストリーム解像度を検出し、Neural SR で再構成します。"},
        // Display
        {"Output", "出力"},
        {"Better GFN Neural shows the enhanced picture in a click-through overlay exactly over GeForce NOW. Mouse, keyboard and controllers keep working normally.",
         "Better GFN Neural は強化した映像を、GeForce NOW のちょうど真上にクリック透過のオーバーレイとして表示します。マウス・キーボード・コントローラーは普段どおり使えます。"},
        {"Output mode", "出力モード"},
        {"Match GFN window", "GFN ウィンドウに合わせる"},
        {"Fullscreen (upscale to monitor)", "フルスクリーン（モニターへアップスケール）"},
        {"Auto: when GeForce NOW runs fullscreen the overlay matches it; when it runs in a smaller window the picture is upscaled to the whole monitor (the cursor is kept inside the game window and drawn scaled).",
         "自動：GeForce NOW が全画面のときはそれに合わせ、小さいウィンドウのときはモニター全体に拡大して表示します（カーソルはゲームウィンドウ内に保たれ、拡大して描画されます）。"},
        {"Output resolution", "出力解像度"},
        {"Source", "ソースと同じ"},
        {"Before / after split view", "比較表示（左: 元映像 / 右: 強化後）"},
        {"Capture method", "キャプチャ方式"},
        {"Monitors", "モニター"},
        {"Preferred monitor", "優先モニター"},
        {"Automatic (monitor showing GeForce NOW)", "自動（GeForce NOW を表示中のモニター）"},
        {"Monitor", "モニター"},
        {"Resolution", "解像度"},
        {"Refresh", "リフレッシュレート"},
        {"Peak / SDR white", "ピーク / SDR 白"},
        {"Color depth", "色深度"},
        {"VRR / tearing", "VRR / ティアリング"},
        {"Supported (off)", "対応（オフ）"},
        {"Not supported", "非対応"},
        {"Capable", "対応"},
        {"Not available", "利用不可"},
        {"Refresh rate is read from the active display mode (e.g. 60, 120, 144, 165, 240 Hz) and used by Auto Mode and frame pacing. VRR-capable presentation is reported by DXGI (tearing support); the overlay is composed by Windows, so VRR follows the desktop compositor.",
         "リフレッシュレートは現在の表示モード（60 / 120 / 144 / 165 / 240 Hz など）から読み取り、オートモードとフレームペーシングに使います。VRR 対応の表示は DXGI（ティアリング対応）で判定します。オーバーレイは Windows が合成するため、VRR はデスクトップの合成処理に従います。"},
        {"Primary", "プライマリ"},
        // Games
        {"Game profiles", "ゲームプロファイル"},
        {"Created automatically from the GeForce NOW window title. Built-in tuning for popular games.",
         "GeForce NOW のウィンドウタイトルから自動で作成されます。人気タイトルには専用の調整が組み込まれています。"},
        {"Built-in profile tuned for this game. You can change everything.", "このゲーム向けに調整済みの組み込みプロファイルです。すべて変更できます。"},
        {"Use global settings", "グローバル設定を使用"},
        {"Performance priority", "パフォーマンス優先度"},
        {"Quality first", "画質優先"},
        {"Latency first", "遅延優先"},
        {"Delete profile", "プロファイルを削除"},
        {"sessions", "回プレイ"},
        {"Built-in", "組み込み"},
        {"Global", "グローバル"},
        {"No game profiles yet. Profiles are created automatically when a game is detected.", "まだプロファイルがありません。ゲームを検出すると自動的に作成されます。"},
        {"Supported out of the box: Cyberpunk 2077, Fortnite, Forza Horizon, Call of Duty, Minecraft, Apex Legends, Counter-Strike, Baldur's Gate 3, The Witcher 3, Rocket League. Any other game gets a profile automatically.",
         "標準対応: Cyberpunk 2077、Fortnite、Forza Horizon、Call of Duty、Minecraft、Apex Legends、Counter-Strike、Baldur's Gate 3、The Witcher 3、Rocket League。その他のゲームにも自動でプロファイルが作られます。"},
        // Performance page
        {"Input FPS", "入力 FPS"},
        {"Output FPS", "出力 FPS"},
        {"Capture FPS", "キャプチャ FPS"},
        {"Input resolution", "入力解像度"},
        {"Processing resolution", "処理解像度"},
        {"stream {}p detected", "ストリーム {}p を検出"},
        {"on {}", "表示先 {}"},
        {"Render time", "処理時間"},
        {"{:.2f} ms avg  \xC2\xB7  {:.2f} p95  \xC2\xB7  {:.2f} max", "平均 {:.2f} ms  \xC2\xB7  p95 {:.2f}  \xC2\xB7  最大 {:.2f}"},
        {"GPU usage", "GPU 使用率"},
        {"n/a", "取得不可"},
        {"{:.0f} MB used by app ({:.0f} MB pipeline) / budget {:.0f} MB", "アプリ使用量 {:.0f} MB（パイプライン {:.0f} MB）/ 予算 {:.0f} MB"},
        {"Frame time", "フレーム時間"},
        {"Upscale mode", "アップスケール方式"},
        {"reduced by Auto Mode", "オートモードにより軽量化"},
        {"active (2x)", "動作中（2x）"},
        {"standby", "待機"},
        {"not beneficial at this refresh rate", "このリフレッシュレートでは効果なし"},
        {"Dropped frames", "ドロップフレーム"},
        {"Added latency", "追加処理遅延"},
        {"Quality tier", "品質ティア"},
        {"{} ({}/6)  \xC2\xB7  budget {:.1f} ms", "{}（{}/6）  \xC2\xB7  予算 {:.1f} ms"},
        {"HDR stream \xE2\x86\x92 HDR display", "HDR ストリーム \xE2\x86\x92 HDR ディスプレイ"},
        {"HDR+ (SDR \xE2\x86\x92 HDR)", "HDR+（SDR \xE2\x86\x92 HDR）"},
        {"SDR Enhancement", "SDR エンハンス"},
        {"GPU time per stage", "ステージ別 GPU 時間"},
        {"Auto Mode decision", "オートモードの判断"},
        {"Auto Mode keeps our GPU work below the budget (a share of the input frame interval), watches VRAM, GPU load and latency, and lowers the quality tier gradually under load. Frame interpolation is paused first.",
         "オートモードは GPU 処理を予算（入力フレーム間隔の一定割合）以内に保ち、VRAM・GPU 負荷・遅延を見ながら、負荷が高いときは品質ティアを少しずつ下げます。最初にフレーム補間を一時停止します。"},
        {"Pipeline", "パイプライン"},
        // GPU stages
        {"Ingest", "取り込み"},
        {"Pyramid", "縮小ピラミッド"},
        {"Optical flow", "オプティカルフロー"},
        {"Compression cleanup", "圧縮ノイズ除去"},
        {"Temporal", "時間方向再構成"},
        {"Super resolution", "超解像"},
        {"Sharpen + Color", "シャープ + カラー"},
        {"Frame interpolation", "フレーム補間"},
        // Quality tiers / upscalers
        {"Minimal", "最小"},
        {"Light", "ライト"},
        {"Balanced Lite", "バランス（軽量）"},
        {"Native (no upscaling)", "ネイティブ（アップスケールなし）"},
        {"Bilinear", "バイリニア"},
        {"Fast Reconstruct (Lanczos-AR)", "高速再構成（Lanczos-AR）"},
        // Engine state / errors (composed by the engine, translated with trText)
        {"Starting", "開始中"},
        {"Preparing", "準備中"},
        {"Paused (system sleep)", "一時停止（スリープ中）"},
        {"Waiting for a GeForce NOW game", "GeForce NOW のゲームを待機中"},
        {"Waiting for GPU", "GPU を待機中"},
        {"Reconnecting capture", "キャプチャを再接続中"},
        {"Capture failed - retrying", "キャプチャに失敗 - 再試行中"},
        {"Ready (GeForce NOW not in focus)", "準備完了（GeForce NOW が非アクティブ）"},
        {"GPU initialization failed", "GPU の初期化に失敗しました"},
        {"Out of GPU memory - lowering quality", "GPU メモリ不足 - 品質を下げています"},
        {"the GFN monitor is driven by a different GPU (Desktop Duplication needs the same adapter)",
         "GeForce NOW を表示しているモニターが別の GPU に接続されています（Desktop Duplication には同じ GPU が必要です）"},
        {"Windows Graphics Capture is not supported on this system", "このシステムは Windows Graphics Capture に対応していません"},
        {"GeForce NOW was not found. Install it from nvidia.com or set its location in Settings.",
         "GeForce NOW が見つかりません。nvidia.com からインストールするか、設定で場所を指定してください。"},
        {"Settings were restored from the last good backup.", "設定を最後の正常なバックアップから復元しました。"},
        {"Settings could not be read and were reset to defaults.", "設定を読み込めなかったため、初期設定に戻しました。"},
        // Auto Mode decisions
        {"Auto Mode active", "オートモード動作中"},
        {"Fixed quality (Auto Mode off)", "固定品質（オートモード オフ）"},
        {"VRAM pressure", "VRAM 逼迫"},
        {"GPU overloaded", "GPU 過負荷"},
        {"Over GPU budget", "GPU 予算超過"},
        {"GPU headroom", "GPU に余裕あり"},
        {"VRAM pressure: frame interpolation paused", "VRAM 逼迫: フレーム補間を一時停止"},
        {"GPU overloaded: frame interpolation paused", "GPU 過負荷: フレーム補間を一時停止"},
        {"Over GPU budget: frame interpolation paused", "GPU 予算超過: フレーム補間を一時停止"},
        {"Frame interpolation enabled", "フレーム補間を有効化"},
        {"Frame interpolation paused (latency/GPU budget)", "フレーム補間を一時停止（遅延 / GPU 予算）"},
        {"Frame interpolation not beneficial at this refresh rate", "このリフレッシュレートではフレーム補間の効果なし"},
        // Benchmark
        {"Runs the complete enhancement pipeline on a synthetic cloud-game scene at every quality tier and measures real GPU time. Takes about 20-60 seconds; GeForce NOW can stay open.",
         "クラウドゲーム風の合成シーンで全処理パイプラインを品質ティアごとに実行し、実際の GPU 時間を測定します。所要時間は約 20〜60 秒です。GeForce NOW は開いたままで構いません。"},
        {"Run benchmark", "ベンチマークを実行"},
        {"Cancel", "キャンセル"},
        {"Average processing time", "平均処理時間"},
        {"Maximum processing time", "最大処理時間"},
        {"Recommended preset", "推奨プリセット"},
        {"Recommended output resolution", "推奨出力解像度"},
        {"Recommended frame interpolation", "推奨フレーム補間"},
        {"Apply recommendation", "推奨設定を適用"},
        {"No benchmark results yet.", "ベンチマーク結果はまだありません。"},
        {"Test", "テスト"},
        {"Backend", "バックエンド"},
        {"Average GPU time per frame (60 fps stream budget: 8.3 ms)", "1 フレームあたりの平均 GPU 時間（60 fps ストリームの予算: 8.3 ms）"},
        {"Done", "完了"},
        {"Cancelled", "キャンセルしました"},
        {"No Direct3D 11 GPU available", "Direct3D 11 対応の GPU がありません"},
        {"Pipeline initialization failed", "パイプラインの初期化に失敗しました"},
        {"Out of GPU memory", "GPU メモリ不足"},
        {"Out of GPU memory at the selected output resolution", "選択した出力解像度では GPU メモリが不足しています"},
        // Settings
        {"Startup", "スタートアップ"},
        {"Start with Windows", "Windows 起動時に開始"},
        {"Windows sign-in \xE2\x86\x92 Better GFN Neural starts in the tray \xE2\x86\x92 waits for GeForce NOW \xE2\x86\x92 enhances automatically.",
         "Windows にサインイン \xE2\x86\x92 Better GFN Neural がタスクトレイで起動 \xE2\x86\x92 GeForce NOW を待機 \xE2\x86\x92 自動で画質強化。"},
        {"Start in the background (tray)", "バックグラウンド（タスクトレイ）で開始"},
        {"Close button minimizes to tray", "閉じるボタンでタスクトレイに格納"},
        {"Start enhancing automatically when a game starts", "ゲーム開始時に自動で画質強化を開始"},
        {"Show notifications", "通知を表示"},
        {"Language", "言語"},
        {"Japanese + English", "日本語 + English"},
        {"GeForce NOW detection", "GeForce NOW 検出"},
        {"Also detect GeForce NOW in web browsers", "Web ブラウザ版 GeForce NOW も検出"},
        {"GeForce NOW location", "GeForce NOW の場所"},
        {"Not found - enter GeForceNOW.exe path", "見つかりません - GeForceNOW.exe のパスを入力"},
        {"Detection is read-only: window titles of GeForce NOW only. Nothing is injected into GeForce NOW and no NVIDIA servers are contacted.",
         "検出は読み取りのみです（GeForce NOW のウィンドウタイトルだけを見ます）。GeForce NOW への注入や NVIDIA のサーバーへの通信は一切行いません。"},
        {"Controllers", "コントローラー"},
        {"Detected for information only - input always goes directly to GeForce NOW.", "表示のための検出のみです。入力は常に GeForce NOW へ直接届きます。"},
        {"No controllers detected", "コントローラーは検出されていません"},
        {"Logs", "ログ"},
        {"Logs never contain account data; the Windows user name and profile path are redacted.",
         "ログにアカウント情報は含まれません。Windows のユーザー名とプロファイルのパスは伏せ字になります。"},
        {"Log level", "ログレベル"},
        {"Debug", "デバッグ"},
        {"Info", "情報"},
        {"Warning", "警告"},
        {"Error", "エラー"},
        {"Open logs folder", "ログフォルダを開く"},
        {"About", "このアプリについて"},
        {"GeForce NOW is a trademark of NVIDIA Corporation. This app only post-processes the picture shown on your own PC; it does not modify GeForce NOW, its servers, accounts, queues, session limits or DRM.",
         "GeForce NOW は NVIDIA Corporation の商標です。本アプリはお使いの PC に表示された映像を後処理するだけで、GeForce NOW 本体・サーバー・アカウント・待機列・セッション時間・DRM には一切手を加えません。"},
        {"Data folder: {}{}", "データフォルダ: {}{}"},
        {"  (portable)", "（ポータブル）"},
        {"License: MIT. Third-party: Dear ImGui (MIT), nlohmann/json (MIT), stb (Public Domain/MIT). See THIRD_PARTY_NOTICES.md.",
         "ライセンス: MIT。サードパーティ: Dear ImGui (MIT)、nlohmann/json (MIT)、stb (Public Domain/MIT)。詳細は THIRD_PARTY_NOTICES.md をご覧ください。"},
        {"Reset all settings", "すべての設定をリセット"},
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
        {"No hardware GPU found (software fallback)", "ハードウェア GPU が見つかりません（ソフトウェア処理）"},
        {"Not found", "見つかりません"},
        {"Installed (not running)", "インストール済み（未起動）"},
        {"Everything runs automatically. Start a game in GeForce NOW and it will be enhanced.", "すべて自動で動作します。GeForce NOW でゲームを開始すると画質強化が始まります。"},
        {"Unofficial companion app. Not affiliated with or endorsed by NVIDIA.", "非公式コンパニオンアプリです。NVIDIA とは関係がなく、承認も受けていません。"},
    };
    return t;
}

// Japanese text -> English original (for the bilingual hints).
const std::unordered_map<std::string_view, std::string_view>& englishByJapanese() {
    static const std::unordered_map<std::string_view, std::string_view> m = [] {
        // Several English strings can share one translation ("Output FPS" and the
        // tile label "OUTPUT FPS"); prefer the normally cased one.
        auto allCaps = [](std::string_view v) {
            for (char ch : v)
                if (ch >= 'a' && ch <= 'z') return false;
            return true;
        };
        std::unordered_map<std::string_view, std::string_view> r;
        for (const auto& [en, ja] : japanese()) {
            if (en == ja) continue;
            auto [it, inserted] = r.emplace(ja, en);
            if (!inserted && allCaps(it->second) && !allCaps(en)) it->second = en;
        }
        return r;
    }();
    return m;
}

// Prefixes of composed error messages; the variable tail stays as is.
constexpr std::pair<std::string_view, std::string_view> kPrefixes[] = {
    {"Windows Graphics Capture failed: ", "Windows Graphics Capture が失敗しました: "},
    {"Desktop Duplication unavailable: ", "Desktop Duplication を利用できません: "},
    {"AcquireNextFrame failed: ", "フレームの取得に失敗しました（AcquireNextFrame）: "},
    {"capture frame error: ", "キャプチャフレームのエラー: "},
    {"GeForce NOW could not be started (", "GeForce NOW を起動できませんでした ("},
};
} // namespace

void setUiLanguage(UiLanguage lang) { gLang = lang; }
UiLanguage uiLanguage() { return gLang; }

UiLanguage resolveLanguage(const std::string& setting) {
    if (setting == "ja") return UiLanguage::Japanese;
    if (setting == "en") return UiLanguage::English;
    if (setting == "ja+en") return UiLanguage::Bilingual;
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(id) == LANG_JAPANESE) return UiLanguage::Bilingual;
#endif
    return UiLanguage::English;
}

const std::unordered_map<std::string_view, const char*>& japaneseTable() { return japanese(); }

const char* tr(const char* english) {
    if (gLang == UiLanguage::English) return english;
    const auto& t = japanese();
    auto it = t.find(english);
    return it == t.end() ? english : it->second;
}

std::string trText(std::string_view s) {
    if (gLang == UiLanguage::English || s.empty()) return std::string(s);
    const auto& t = japanese();
    if (auto it = t.find(s); it != t.end()) return it->second;
    // "<reason> (<tier> -> <tier>)" from the Auto Mode controller
    if (s.back() == ')') {
        const size_t open = s.rfind(" (");
        const size_t arrow = open == std::string_view::npos ? open : s.find(" -> ", open);
        if (arrow != std::string_view::npos) {
            return trText(s.substr(0, open)) + "（" + trText(s.substr(open + 2, arrow - open - 2)) + " \xE2\x86\x92 " +
                   trText(s.substr(arrow + 4, s.size() - arrow - 5)) + "）";
        }
    }
    for (const auto& [en, ja] : kPrefixes)
        if (s.starts_with(en)) return std::string(ja) + std::string(s.substr(en.size()));
    // Benchmark progress: "Measuring <tier> quality"
    constexpr std::string_view m1 = "Measuring ", m2 = " quality";
    if (s.size() > m1.size() + m2.size() && s.starts_with(m1) && s.ends_with(m2))
        return trText(s.substr(m1.size(), s.size() - m1.size() - m2.size())) + " を測定中";
    // Benchmark recommendation: "<w>x<h> (monitor)"
    constexpr std::string_view mon = " (monitor)";
    if (s.ends_with(mon)) return std::string(s.substr(0, s.size() - mon.size())) + "（モニター）";
    return std::string(s);
}

const char* englishFor(const char* shown) {
    if (gLang != UiLanguage::Bilingual || !shown) return nullptr;
    const auto& m = englishByJapanese();
    auto it = m.find(shown);
    return it == m.end() ? nullptr : it->second.data(); // keys of the table are string literals
}

} // namespace bgn
