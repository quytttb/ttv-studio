# Handoff — TTV Studio

## Repo này là gì

Desktop app **Qt 6.11 / C++20 / QML** sản xuất video AI:

- **Render**: text script → TTS (master clock) → LLM chia cảnh → Veo clips → MP4
- **Redub**: URL/MP4 → Whisper local STT → LLM dịch duration-aware → TTS dub → assembly original-clock

Greenfield C++; behavior contract tham khảo từ repo Python `text_to_video`
(`docs/REDUB-PIPELINE.md`) — **không port source/tests Python**.

## Đọc gì đầu tiên

| File | Nội dung |
|------|----------|
| [`README.md`](README.md) | Kiến trúc, pipelines, build, CI |
| [`AGENTS.md`](AGENTS.md) | Quy tắc bắt buộc khi implement (kiến trúc, constants, testing) |

## Kiến trúc & durability

- Tầng static libs: `utils → media → providers → jobs → render/redub → core → components/app`
- **State chart** (`src/jobs/JobTypes.h`): mọi transition đi qua `JobStore::updateJob` với validate `canTransition`; recovery states (`WAITING_FOR_PROVIDER`/`UNKNOWN_PROVIDER_STATE`) bắt buộc ghi `pending_state` và chỉ được resume về đúng đó hoặc abort
- **JobStore**: `storage/jobs/<id>/{input,work,output}` + `job.json`, ghi atomic `.part→rename` (QSaveFile)
- **Providers** (`src/providers/`): lỗi chuẩn hoá 3 kind — Transient (retry), Permanent (fail), AmbiguousTimeout (submit/poll timeout, remote state unknown → persist task id, reconcile chứ không resubmit — "never pay twice")
- Video generation lưu provider task id vào `timeline/scenes.json` ngay sau submit
- HTTP stack dựng trên worker thread chạy nó (QNAM affinity) — xem `RenderController::startRun`

## Quyết định đã chốt

| Chủ đề | Quyết định |
|--------|------------|
| Whisper STT | whisper.cpp CLI subprocess (`TTV_STUDIO_WHISPER_BIN`/`_MODEL`), không vendor SDK |
| Retry | Exponential backoff + jitter cap 8s, chỉ retry Transient; sleep injectable cho test |
| Transport | `ITransport` boundary + `QNamTransport` (blocking QEventLoop); timeout do caller là authority duy nhất |
| Dub timing | atempo clamp band 0.85–1.25; spill-over flag cho UI; fail closed khi clip lệch nặng |
| Scene timing | DP quantize về durations gateway hỗ trợ ({4,6,8}s default), clip-count penalty, retime policy 1.10 |
| Coverage | Nối narration các scene phải tái tạo script chính xác từng ký tự (whitespace-insensitive) — LLM chỉ được *split* |
| Config | env var → SettingsStore (QSettings) → AppConstants default |
| UI kit | LoggerKit (git submodule) + `TtvStudio.Components`; frameless window, Material light/dark |

## Trạng thái: P1–P5 HOÀN TẤT

| M | Commit | Nội dung |
|---|--------|----------|
| P1 | `d6c7b54` | Subprocess/Ffprobe · state machine · JobStore atomic |
| P2 | `3d9e1fb` | LLM/TTS/Veo clients + retry/phân loại lỗi + redaction |
| P3a/b/c | `3eb4f83`→`bf8ba6c` | Render engine core → orchestrator → RenderPage UI |
| P4a/b/c | `62cd569`→`c42a79a` | Translator → yt-dlp/Whisper/RedubPipeline → RedubPage UI |
| P5 | `cee59b5` | SettingsStore + Settings page + endpoint merge (env→setting→default) |
| docs/cleanup | `939ca9f`+ | README mới; bỏ domain logger cũ khỏi docs/constants/components |

25 test suites xanh trên CI; qmllint sạch; boot offscreen không warning.

## Việc còn mở

1. Bundle ffmpeg/yt-dlp/whisper vào `.deb`/installer (app đọc `TTV_STUDIO_FFMPEG_BIN_DIR`, `TTV_YTDLP_BIN`, `TTV_STUDIO_WHISPER_BIN` — installer chỉ cần drop binary)
2. Concurrent scene generation trong video stage (hiện sequential)
3. Original-audio sidechain ducking cho Redub (v1: dub-only track)
4. Providers phụ: Gemini STT, Imagen Ken Burns
5. Quality gates chặn COMPLETED (probe duration/audio checks có sẵn trong verifying stage)

## Gotchas đã va phải (đừng lặp lại)

- `QFile::rename` **không ghi đè** file tồn tại → dùng `QSaveFile` cho artifact cập nhật nhiều lần
- ffmpeg **không tự tạo thư mục cha** → caller luôn `mkpath`
- Output đuôi `.part` làm ffmpeg không đoán được muxer → thêm `-f wav/mp4` tường minh
- Server `Connection: close` gây benign `RemoteHostClosedError` → transport chấp nhận khi có status line
- Submit timeout = AmbiguousTimeout (không retry mù quáng); poll timeout cũng vậy — budget-based reconcile
- QNAM phải sống trên thread gọi nó; pipeline blocking chạy trên worker, signals queued về GUI
