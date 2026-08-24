# Packaging — TTV Studio (Qt 6 C++)

Version lấy từ `project(ttv_studio VERSION …)` trong [`CMakeLists.txt`](../CMakeLists.txt).  
CPack DEB: [`cmake/CPackOptions.cmake`](../cmake/CPackOptions.cmake).

| Thư mục | Script chính | Artifact |
|---------|----------------|----------|
| [`linux/`](linux/) | `cpack_deb.sh`, `deploy.sh` | `dist/ttv-studio-app_<ver>_amd64.deb` |
| [`windows/`](windows/) | `build_installer.ps1`, `deploy.ps1` | `installer_build/TtvStudioSetup.exe` |

**CI:** push `main` → `dev-build.yml` (artifact dev); tag `v*.*.*` → `build-release.yml` (GitHub Release).

---

## Phát hành (git tag → GitHub Actions)

(`scripts/deploy.sh` / `deploy.ps1`): bump SemVer → commit `CMakeLists.txt` → tag `vX.Y.Z` → push → workflow **Build Release** build `.deb` + installer.

| Lệnh | Mô tả |
|------|--------|
| `./packaging/linux/deploy.sh` | Menu tương tác (Linux / WSL / Git Bash) |
| `./packaging/linux/deploy.sh release patch` | Bump PATCH → commit → tag → push |
| `.\packaging\windows\deploy.ps1` | Menu trên Windows |
| `.\packaging\windows\deploy.ps1 release minor` | Bump MINOR → … |

Subcommand: `bump`, `commit`, `tag`, `push-tag`, `status`, `help` (menu **6** — hướng dẫn chi tiết; alias `cheatsheet`).

Bump thủ công:

```bash
python3 packaging/linux/bump_version.py show
python3 packaging/linux/bump_version.py bump patch
```

Re-build khi tag đã có (không đổi git): GitHub → Actions → **Build Release** → Run workflow → nhập `v0.1.0`.

---

## Linux — `.deb` (CPack + Qt Deployment API)

### Yêu cầu

- Qt **6.11** (`CMAKE_PREFIX_PATH`)
- `patchelf`, `dpkg-dev` (cho CPack DEB trên runner/local)

### Build local

```bash
export CMAKE_PREFIX_PATH=~/Qt/6.11.2/gcc_64
./packaging/linux/cpack_deb.sh
# → dist/ttv-studio-app_<version>_amd64.deb
```

Script: Release CMake → `cpack -G DEB` (Qt `qt_generate_deploy_app_script` khi `cmake --install` trong CPack).  
Desktop entry: `linux/ttv-studio.desktop` + icon SVG từ `resources/icons/`.

### Cài đặt

```bash
sudo apt install ./dist/ttv-studio-app_*_amd64.deb
```

Chạy: `ttv_studio` hoặc launcher **TTV Studio** trong menu ứng dụng.

---

## Windows — QTIFW installer

Đóng gói **Qt Installer Framework** (offline `TtvStudioSetup.exe`), ~100MB+, chạy trên Windows sạch không cần Qt.

### Cấu trúc `windows/installer_build/`

```text
packaging/windows/installer_build/
├── config/
│   ├── config.xml      # Tên app, publisher quytttb, TargetDir, wizard
│   ├── style.qss       # Giao diện installer (dark + Teal)
│   ├── logo.png
│   ├── window_icon.png
│   └── app_icon.ico
├── packages/com.ttv_studio.app/
│   ├── meta/package.xml, installscript.qs   # Shortcuts Desktop / Start Menu
│   └── data/                                 # Nội dung sau windeployqt (build tạo)
└── TtvStudioSetup.exe                    # Đầu ra
```

Logo/icon sinh từ `resources/icons/brand_4m_technologies_blue.svg` qua [`windows/convert_tool/`](windows/convert_tool/).

### Build local

1. Build **Release** trong Qt Creator (target `app` → `ttv_studio.exe`).
2. Từ root repo:

```powershell
.\packaging\windows\build_installer.ps1
```

Script: kiểm tra `ttv_studio.exe` + `windeployqt` + `binarycreator` → deploy QML/DLL vào `installer_build/.../data` → tạo `TtvStudioSetup.exe`.

**CI:** `CL_PROJECT_ROOT`, `CL_QT_DIR`, `CL_BUILD_DIR`, `CL_MINGW_DIR`; `CL_IFW_DIR` tuỳ chọn (tự tìm `binarycreator.exe` dưới `IQTA_TOOLS`).

Mặc định exe Release: `build/Desktop_Qt_6_11_1_MinGW_64_bit-Release/bin/` hoặc `build-dev-win/bin/` (CI).

### Cài đặt cho người dùng

1. Chạy `TtvStudioSetup.exe`.
2. Thư mục mặc định thường `C:\TtvStudio`.
3. Installer tạo shortcut Desktop và Start Menu.

---

## File tham chiếu

| File | Vai trò |
|------|---------|
| `linux/cpack_deb.sh` | Build `.deb` |
| `linux/bump_version.py` | SemVer trong CMakeLists |
| `linux/sync_cmake_version.py` | CI: `VERSION` env → `bump_version set` |
| `linux/deploy.sh` | Release git + push tag |
| `windows/build_installer.ps1` | QTIFW package |
| `windows/deploy.ps1` | Release git (Windows) |
