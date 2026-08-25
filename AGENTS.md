# Agent instructions — TTV Studio

## Context

Desktop app **Qt 6.11 / C++20 / QML** cho sản xuất video AI, hai pipeline:

- **Render**: text script → TTS (master clock) → LLM chia cảnh → Veo clips → MP4
- **Redub**: URL/MP4 → Whisper local STT → LLM dịch duration-aware → TTS dub → assembly original-clock

Tham khảo behavior chi tiết: repo `text_to_video` (Python) — `docs/REDUB-PIPELINE.md`.
**Cấm** port source/tests Python; chỉ tham khảo contract hành vi.

## Kiến trúc (bắt buộc)

```
src/media      Subprocess/Ffprobe/MediaEngine/YtDlp/WhisperStt — wrapper typed, không business logic
src/providers  REST clients + Retry + ProviderError (Transient/Permanent/AmbiguousTimeout) + redact secrets
src/jobs       State chart (JobTypes) + JobRecord/JobStore — persist atomic .part→rename
src/render     ScenePlanner/DurationOptimizer/SceneManifest/Captions/RenderPipeline
src/redub      Transcript/Translator/DubPlanner/RedubPipeline
src/core       RenderController (QML facade, kind-dispatch), JobListModel, SettingsStore, ProviderEndpoints
src/components TtvStudio.Components (chỉ component đặc thù)
src/app        Shell QML: Render / Redub / Settings
```

Quy tắc:
- **MVVM**: logic trong C++ (pipeline/service/controller), QML chỉ binding + view
- Không `.js` logic nghiệp vụ; QML module qua `qt_add_qml_module`, type via `QML_ELEMENT`
- **Không exception** qua biên layer: result struct tường minh (`ok` + `error`)
- Mọi stage transition phải đi qua `JobStore::updateJob` (validate `canTransition`)
- Artifact ghi atomic (`QSaveFile` hoặc `.part→rename`); ffmpeg **không tự tạo dir** — caller `mkpath`
- HTTP client phải được tạo trên thread chạy nó (QNAM affinity) — xem `RenderController::startRun`
- Secrets (API key) luôn redact trước khi vào error message/log

## Constants — single source of truth

Toàn bộ hằng số vận hành nằm trong `src/utils/AppConstants.h`
(`TtvStudio::Defaults::*`) — ports/timeouts/poll intervals/retry budget/
dubbing band/render knobs. Không hardcode số trong source.

Env vars người dùng: `TTV_LLM_*`, `TTV_TTS_BASE_URL`, `TTV_VIDEO_GATEWAY_*`,
`TTV_YTDLP_BIN`, `TTV_INGEST_COOKIES_FILE`, `TTV_STUDIO_FFMPEG_BIN_DIR`,
`TTV_STUDIO_WHISPER_BIN`, `TTV_STUDIO_WHISPER_MODEL`,
`TTV_STUDIO_STORAGE_ROOT`. Thứ tự resolve: env → SettingsStore (QSettings) → default.

## CMake

- `find_package(Qt6 6.11 …)` components: Quick, QuickControls2, Qml, Svg, Network, Test
- Targets: `app` (binary `ttv_studio`) · static libs `utils media providers jobs render redub core components`
- Test helper: `add_ttv_studio_test(NAME … SOURCES … LIBS …)` trong `cmake/TtvStudioTest.cmake`

## Testing

Qt Test mới trong repo này. Quy ước:
- Logic deterministic (planner/optimizer/coverage) test thuần không I/O
- REST clients test qua `FakeTransport` (`tests/providers/FakeTransport.h`) — script theo thứ tự call,
  `sinkPayloadOverrides` theo chỉ số call khi cần nhiều body khác nhau
- Pipeline integration: stub binaries (shell script giả yt-dlp/whisper) + ffmpeg thật;
  `QSKIP` khi runner thiếu ffmpeg/ffprobe
- Sau khi sửa code: `cmake --build build && cd build && ctest --output-on-failure`
  và `cmake --build build --target qmllint`; boot offscreen phải không có warning:
  `QT_QPA_PLATFORM=offscreen timeout 12 ./build/bin/ttv_studio`

## Trạng thái hiện tại

P1–P5 đã hoàn tất (xem `HANDOFF.md`). Việc còn mở: bundle ffmpeg/yt-dlp/whisper
vào deb/installer, concurrent scene generation, original-audio ducking cho Redub,
providers Gemini/Imagen.

## Conventional Commits; không commit trừ khi được yêu cầu.
