# TTV Studio

Desktop app **Qt 6.11 / C++20 / QML** cho sản xuất video bằng AI, với hai pipeline:

| Pipeline | Luồng | Kết quả |
|----------|-------|---------|
| **Render** | Text script → TTS (master clock) → LLM chia cảnh → sinh clip (Veo gateway) → normalize/concat/mux | `output/final_video.mp4` |
| **Redub** | URL (Douyin/XHS/TikTok/YT qua yt-dlp) hoặc MP4 local → Whisper STT → LLM dịch duration-aware → TTS lồng tiếng → assembly original-clock (atempo) | `output/final_video.mp4` |

Nguyên tắc bất biến: **audio là đồng hồ chủ đạo** — clip/narration được retime để khớp timeline, không bao giờ ngược lại.

## Kiến trúc

```
src/
├── media/      Subprocess runner, Ffprobe, MediaEngine (ffmpeg ops), YtDlp, WhisperStt
├── providers/  REST clients: LLM (OpenAI-compatible), TTS :3900, video gateway :8765
│               Retry + phân loại lỗi Transient / Permanent / AmbiguousTimeout
├── jobs/       State chart Render/Redub + JobStore ghi atomic (.part → rename)
├── render/     Scene planning, coverage verification, duration optimizer (DP),
│               captions VTT, RenderPipeline orchestrator
├── redub/      Transcript domain, duration-aware Translator, DubPlanner,
│               RedubPipeline orchestrator
├── core/       RenderController (QML facade), JobListModel, SettingsStore
├── components/ QML components riêng của app (rail/topbar, banners…)
└── app/        Shell QML: Render / Redub / Settings
shared/logger-ui-kit/   UI kit dùng chung (git submodule)
```

**Độ bền (durability):** mỗi stage transition được persist qua `JobStore`; artifact nằm ở
`<storage>/jobs/<id>/{input,work,output}`. Crash/restart → job resume từ stage cuối;
provider task id của video generation được lưu ngay sau submit ("never pay twice").

### State chart

```text
RENDER: CREATED → VALIDATING → TTS_RUNNING → TTS_READY → PLANNING → SCENES_READY
        → VIDEO_RUNNING → CLIPS_READY → POST_PROCESSING → VERIFYING → COMPLETED

REDUB:  CREATED → VALIDATING → INGESTING → SOURCE_READY → TRANSCRIBING
        → TRANSCRIPT_READY → TRANSLATING → TRANSLATION_READY → (đường chung từ TTS_RUNNING)

Terminal/recovery: FAILED · CANCELLED · WAITING_FOR_PROVIDER · UNKNOWN_PROVIDER_STATE
```

## Dịch vụ ngoài & công cụ

| Thành phần | Mặc định | Ghi chú |
|------------|----------|---------|
| LLM (planning/dịch) | `https://api.vilao.ai/v1` | Bất kỳ endpoint OpenAI-compatible nào |
| Local voice TTS | `http://127.0.0.1:3900` | `POST /generate` multipart |
| Video gateway (Veo) | `http://127.0.0.1:8765` | Webhook API submit/poll/download |
| ffmpeg / ffprobe | PATH hoặc `TTV_STUDIO_FFMPEG_BIN_DIR` | |
| yt-dlp | PATH hoặc `TTV_YTDLP_BIN` (fallback `python -m yt_dlp`) | |
| whisper.cpp | `TTV_STUDIO_WHISPER_BIN` + `TTV_STUDIO_WHISPER_MODEL` (ggml .bin) | |

Cấu hình endpoints/keys/models: **tab Settings** trong app (lưu QSettings) hoặc env vars
(`TTV_LLM_*`, `TTV_TTS_BASE_URL`, `TTV_VIDEO_GATEWAY_*`) — **env ưu tiên hơn**.
Secrets trong error message/log luôn được redact.

## Build

Yêu cầu: **Qt 6.11**, CMake 3.16+, Qt modules: Quick, QuickControls2, Qml, Svg, Network, Test
(cài qua [Qt Online Installer](https://doc.qt.io/qt-6/get-and-install-qt.html)).

```bash
export CMAKE_PREFIX_PATH=~/Qt/6.11.2/gcc_64${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}

cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/bin/ttv_studio
```

### Test

```bash
cd build && ctest --output-on-failure
```

25 suites: media (subprocess/ffprobe/engine/yt-dlp), providers (retry/error/LLM/TTS/
gateway/transport), jobs (state machine/store), render (coverage/optimizer/manifest/
captions/planner/pipeline), redub (transcript/translator/planner/pipeline), core
(controller/settings). Test integration dùng fake HTTP transport + stub binaries;
các test cần ffmpeg sẽ tự skip khi thiếu binary trên runner.

### Qt Creator

Mở `CMakeLists.txt` → kit Desktop → run target **`app`** (binary `ttv_studio`).
Sau khi đổi `CMakeLists.txt`: **Run CMake** rồi **Rebuild**.

## CI / đóng gói

Chi tiết Linux + Windows + phát hành: [`packaging/README.md`](packaging/README.md).

| Workflow | Khi chạy | Kết quả |
|----------|----------|---------|
| `ci.yml` | push / PR `main` | cmake Debug + ctest |
| `dev-build.yml` | push `main` | artifact `.deb` + `TtvStudioSetup.exe` |
| `build-release.yml` | tag `v*.*.*` | GitHub Release |

**Release:** `./packaging/linux/deploy.sh` hoặc `.\packaging\windows\deploy.ps1`.

## Roadmap

- [ ] Bundle ffmpeg/yt-dlp/whisper vào `.deb` / installer (app đã đọc các dir cấu hình)
- [ ] Concurrent scene generation trong video stage
- [ ] Original-audio sidechain ducking cho Redub (v1 dùng dub-only track)
- [ ] Providers phụ: Gemini STT, Imagen Ken Burns
