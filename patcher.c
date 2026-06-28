#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdarg.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// ─── DPI 感知 ──────────────────────────────────────────────────
static void InitDPI(void)
{
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL (WINAPI *F)(DPI_AWARENESS_CONTEXT);
        F f = (F)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (f && f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    HMODULE sh = LoadLibraryW(L"shcore.dll");
    if (sh) {
        typedef HRESULT (WINAPI *F2)(int);
        F2 f2 = (F2)GetProcAddress(sh, "SetProcessDpiAwareness");
        if (f2) f2(2);
        FreeLibrary(sh);
        return;
    }
    SetProcessDPIAware();
}

// ─── 补丁定义 ───────────────────────────────────────────────────
typedef struct { DWORD off; BYTE orig[8]; BYTE patch[8]; int len; LPCWSTR desc; } PATCH;

static const PATCH g_patches[] = {
    { 0x0130947, {0x98,0x00,0x00,0x00}, {0x20,0x1B,0x00,0x00}, 4,
      L"许可证 152->6944 [sub_1401312B0]" },
    { 0x0130C0B, {0x98,0x00,0x00,0x00}, {0x20,0x1B,0x00,0x00}, 4,
      L"许可证 152->6944 [sub_1401316D0 / 防覆盖]" },
    { 0x00E78C2, {0x01}, {0x02}, 1,
      L"版本类型: std(1) -> ent(2) [sub_1400E8180]" },
    { 0x00F27BD, {0x74,0x34}, {0x90,0x90}, 2,
      L"关于页: 隐藏购买/注册按钮 [sub_1400F2C10]" },
};
enum { PATCH_N = sizeof(g_patches)/sizeof(g_patches[0]) };

// ─── 资源ID ────────────────────────────────────────────────────
#define IDC_LOG         1001
#define IDC_CHECK       1002
#define IDC_PATCH       1003
#define IDC_VERIFY      1004
#define IDC_BROWSE      1005
#define IDC_PATH        1006

// ─── 全局 ──────────────────────────────────────────────────────
static HWND  g_log, g_btnPatch, g_btnVerify, g_editPath;
static WCHAR g_targetDir[MAX_PATH];
static BOOL  g_checked = FALSE;

// ─── 工具 ──────────────────────────────────────────────────────
static void Log(LPCWSTR fmt, ...)
{
    WCHAR buf[2048]; va_list a; va_start(a, fmt);
    wvsprintfW(buf, fmt, a); va_end(a);
    int n = (int)SendMessageW(g_log, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(g_log, EM_SETSEL, n, n);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)buf);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
}

static BOOL IsBandizipDir(LPCWSTR dir)
{
    WCHAR exe[MAX_PATH], ini[MAX_PATH];
    wsprintfW(exe, L"%s\\Bandizip.exe", dir);
    wsprintfW(ini, L"%s\\config.ini", dir);
    return GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES &&
           GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES;
}

static BOOL CheckEnv(LPCWSTR dir)
{
    SendMessageW(g_log, WM_SETTEXT, 0, (LPARAM)L"");
    Log(L"===== 环境检查 =====");
    Log(L"目标目录: %s", dir);

    if (!IsBandizipDir(dir)) {
        Log(L"");
        Log(L"X 未找到 Bandizip.exe 或 config.ini");
        Log(L"  请确认目录包含 Bandizip 完整安装。");
        return FALSE;
    }

    WCHAR exe[MAX_PATH];
    wsprintfW(exe, L"%s\\Bandizip.exe", dir);
    WIN32_FILE_ATTRIBUTE_DATA attr;
    GetFileAttributesExW(exe, GetFileExInfoStandard, &attr);
    Log(L"  V 找到 Bandizip.exe (%lu bytes)", attr.nFileSizeLow);
    Log(L"  V 找到 config.ini");
    Log(L"");
    Log(L"===== 环境检查通过 =====");
    return TRUE;
}

// ─── 备份 ──────────────────────────────────────────────────────
static BOOL BackupExe(LPCWSTR dir)
{
    WCHAR src[MAX_PATH], dst[MAX_PATH];
    wsprintfW(src, L"%s\\Bandizip.exe", dir);
    wsprintfW(dst, L"%s\\Bandizip.exe.bak", dir);
    if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES) {
        Log(L"  - 备份已存在");
        return TRUE;
    }
    if (CopyFileW(src, dst, TRUE)) {
        Log(L"  V 已备份 Bandizip.exe.bak");
        return TRUE;
    }
    Log(L"  X 备份失败 (err=%lu)", GetLastError());
    return FALSE;
}

// ─── config.ini ────────────────────────────────────────────────
static void PatchConfig(LPCWSTR dir)
{
    WCHAR path[MAX_PATH];
    wsprintfW(path, L"%s\\config.ini", dir);
    HANDLE h = CreateFileW(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { Log(L"  X 无法打开 config.ini"); return; }

    DWORD sz = GetFileSize(h, NULL);
    if (sz > 65536) { CloseHandle(h); return; }
    char *buf = HeapAlloc(GetProcessHeap(), 0, sz+1);
    if (!buf) { CloseHandle(h); return; }
    DWORD rd; ReadFile(h, buf, sz, &rd, NULL); buf[rd]=0; CloseHandle(h);

    char *line = buf; BOOL ok = FALSE;
    while (*line) {
        if (strncmp(line, "config_id", 9) == 0) {
            char *eq = strchr(line, '=');
            if (eq) {
                char *v = eq+1; while (*v==' '||*v=='\t') v++;
                if (strncmp(v,"ent",3)==0) { Log(L"  - config.ini 已是 ent"); HeapFree(GetProcessHeap(),0,buf); return; }
                if (strncmp(v,"std",3)==0) { v[0]='e'; v[1]='n'; v[2]='t'; ok=TRUE; }
            }
            break;
        }
        line = strchr(line,'\n'); if (!line) break; line++;
    }

    if (!ok) { Log(L"  - 未找到 config_id=std"); HeapFree(GetProcessHeap(),0,buf); return; }

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr; WriteFile(h, buf, (DWORD)strlen(buf), &wr, NULL); CloseHandle(h);
        Log(L"  V config.ini: std -> ent");
    }
    HeapFree(GetProcessHeap(),0,buf);
}

// ─── 二进制补丁 ────────────────────────────────────────────────
static BOOL ApplyPatches(LPCWSTR dir)
{
    WCHAR path[MAX_PATH];
    wsprintfW(path, L"%s\\Bandizip.exe", dir);
    HANDLE h = CreateFileW(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { Log(L"  X 无法打开 Bandizip.exe"); return FALSE; }

    BOOL all = TRUE;
    for (int i = 0; i < PATCH_N; i++) {
        const PATCH *p = &g_patches[i];
        BYTE cur[8]={0}; DWORD rd;
        SetFilePointer(h, p->off, NULL, FILE_BEGIN);
        ReadFile(h, cur, p->len, &rd, NULL);

        if (memcmp(cur, p->patch, p->len) == 0) { Log(L"  - 已补丁: %s", p->desc); continue; }
        if (memcmp(cur, p->orig, p->len) != 0) {
            Log(L"  ! 跳过 (%s): 数据不匹配 [%02X%02X]", p->desc, cur[0],cur[1]);
            all = FALSE; continue;
        }
        SetFilePointer(h, p->off, NULL, FILE_BEGIN);
        DWORD wr; WriteFile(h, p->patch, p->len, &wr, NULL);
        Log(L"  V 补丁: %s", p->desc);
    }
    CloseHandle(h);
    return all;
}

// ─── 验证 ──────────────────────────────────────────────────────
static BOOL VerifyPatches(LPCWSTR dir)
{
    WCHAR path[MAX_PATH];
    wsprintfW(path, L"%s\\Bandizip.exe", dir);
    HANDLE h = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok = TRUE;
    for (int i = 0; i < PATCH_N; i++) {
        const PATCH *p = &g_patches[i];
        BYTE cur[8]={0}; DWORD rd;
        SetFilePointer(h, p->off, NULL, FILE_BEGIN);
        ReadFile(h, cur, p->len, &rd, NULL);
        if (memcmp(cur, p->patch, p->len)==0) Log(L"  V %s", p->desc);
        else { Log(L"  X 未补丁: %s", p->desc); ok=FALSE; }
    }
    CloseHandle(h);
    return ok;
}

// ─── 对话框 ────────────────────────────────────────────────────
static INT_PTR CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_INITDIALOG:
        g_log       = GetDlgItem(h, IDC_LOG);
        g_btnPatch  = GetDlgItem(h, IDC_PATCH);
        g_btnVerify = GetDlgItem(h, IDC_VERIFY);
        g_editPath  = GetDlgItem(h, IDC_PATH);
        EnableWindow(g_btnPatch, FALSE);
        EnableWindow(g_btnVerify, FALSE);

        { RECT r; GetWindowRect(h, &r);
          SetWindowPos(h, NULL,
            (GetSystemMetrics(SM_CXSCREEN)-(r.right-r.left))/2,
            (GetSystemMetrics(SM_CYSCREEN)-(r.bottom-r.top))/2,
            0,0,SWP_NOSIZE|SWP_NOZORDER); }

        Log(L"Bandizip 通用破解补丁 v1.0");
        Log(L"适用: Bandizip 7.x Standard (x64)");
        Log(L"----------------------------------------------");
        Log(L"请先 [浏览] 选择 Bandizip 安装目录。");
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_BROWSE: {
            BROWSEINFOW bi = {0};
            bi.lpszTitle = L"选择 Bandizip 安装目录";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                SHGetPathFromIDListW(pidl, g_targetDir);
                CoTaskMemFree(pidl);
                SetWindowTextW(g_editPath, g_targetDir);
            }
            return TRUE;
        }
        case IDC_CHECK: {
            GetWindowTextW(g_editPath, g_targetDir, MAX_PATH);
            g_checked = CheckEnv(g_targetDir);
            EnableWindow(g_btnPatch, g_checked);
            EnableWindow(g_btnVerify, g_checked);
            return TRUE;
        }
        case IDC_PATCH:
            GetWindowTextW(g_editPath, g_targetDir, MAX_PATH);
            if (!g_checked && !CheckEnv(g_targetDir)) {
                MessageBoxW(h, L"请先通过环境检查。", L"提示", MB_ICONINFORMATION);
                return TRUE;
            }
            if (IDNO == MessageBoxW(h,
                L"即将对 Bandizip.exe 应用以下修改:\r\n\r\n"
                L"* 许可证哈希: Standard → Enterprise\r\n"
                L"* 版本类型:   std → ent\r\n"
                L"* 关于页面:   隐藏购买/注册按钮\r\n\r\n"
                L"原始文件备份为 Bandizip.exe.bak\r\n\r\n确认继续?",
                L"确认破解", MB_ICONQUESTION|MB_YESNO))
                return TRUE;

            SendMessageW(g_log, WM_SETTEXT, 0, (LPARAM)L"");
            Log(L"===== 执行破解 =====");
            BackupExe(g_targetDir);
            PatchConfig(g_targetDir);
            Log(L"");
            BOOL ok = ApplyPatches(g_targetDir);
            Log(L"");
            Log(ok ? L"===== 完成 =====" : L"===== 部分失败 =====");
            MessageBoxW(h, ok ?
                L"破解成功!\r\n\r\n* 许可证 -> 企业版\r\n* 版本 -> ent\r\n* 按钮 -> 隐藏\r\n\r\n原文件备份为 .bak" :
                L"部分补丁失败，查看日志。",
                ok ? L"成功" : L"警告", ok ? MB_ICONINFORMATION : MB_ICONWARNING);
            return TRUE;

        case IDC_VERIFY:
            GetWindowTextW(g_editPath, g_targetDir, MAX_PATH);
            SendMessageW(g_log, WM_SETTEXT, 0, (LPARAM)L"");
            Log(L"===== 验证补丁 =====");
            if (VerifyPatches(g_targetDir)) {
                Log(L""); Log(L"===== 全部通过 =====");
                MessageBoxW(h, L"所有补丁已正确应用。", L"验证通过", MB_ICONINFORMATION);
            } else {
                Log(L""); Log(L"===== 存在未应用补丁 =====");
                MessageBoxW(h, L"部分补丁未应用，请重新破解。", L"验证失败", MB_ICONWARNING);
            }
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(h, 0);
        return TRUE;
    }
    return FALSE;
}

// ─── 入口 ──────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cl, int ns)
{
    InitDPI();
    InitCommonControls();

    // 自动检测当前目录
    GetCurrentDirectoryW(MAX_PATH, g_targetDir);

    return (int)DialogBoxParamW(hi, MAKEINTRESOURCEW(100), NULL, DlgProc, 0);
}
