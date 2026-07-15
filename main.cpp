#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <random>
#include <fstream>
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
#include <shobjidl.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace fs = std::filesystem;

int g_Language = 0;
std::string g_Status = "Ожидание действий... / Waiting for actions...";
std::string g_SelectedPath = "";
ImVec4 g_AccentColor = ImVec4(0.15f, 0.16f, 0.21f, 1.00f);

const char* T(const char* ru, const char* en) {
    return g_Language == 0 ? ru : en;
}

std::string GetConfigPath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path = buffer;
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        return path.substr(0, pos) + "\\sysnova.ini";
    }
    return "sysnova.ini";
}

void LoadSettings() {
    std::ifstream file(GetConfigPath());
    if (file.is_open()) {
        file >> g_Language;
        file >> g_AccentColor.x >> g_AccentColor.y >> g_AccentColor.z;
        file.close();
    }
}

void SaveSettings() {
    std::ofstream file(GetConfigPath());
    if (file.is_open()) {
        file << g_Language << "\n";
        file << g_AccentColor.x << " " << g_AccentColor.y << " " << g_AccentColor.z << "\n";
        file.close();
    }
}

#define CCH_RM_SESSION_KEY 32
#define RmForceShutdown 1

typedef DWORD(WINAPI *RmStartSession_t)(DWORD*, DWORD, WCHAR[]);
typedef DWORD(WINAPI *RmRegisterResources_t)(DWORD, UINT, LPCWSTR*, UINT, PVOID, UINT, PVOID);
typedef DWORD(WINAPI *RmShutdown_t)(DWORD, ULONG, PVOID);
typedef DWORD(WINAPI *RmEndSession_t)(DWORD);

float ClampColor(float v) { return v > 1.0f ? 1.0f : (v < 0.0f ? 0.0f : v); }

void ApplyAccentColor(ImVec4 color) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = color;
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(ClampColor(color.x + 0.05f), ClampColor(color.y + 0.05f), ClampColor(color.z + 0.05f), 1.0f);
    style.Colors[ImGuiCol_ButtonActive]  = ImVec4(ClampColor(color.x + 0.10f), ClampColor(color.y + 0.10f), ClampColor(color.z + 0.10f), 1.0f);
    style.Colors[ImGuiCol_Tab] = color;
    style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_ButtonHovered];
    style.Colors[ImGuiCol_TabActive] = style.Colors[ImGuiCol_ButtonActive];
}

void ExecCmd(const std::string& cmd, const std::string& successMsg) {
    std::string fullCmd = "cmd.exe /c " + cmd;
    UINT res = WinExec(fullCmd.c_str(), SW_HIDE);
    if (res > 31) g_Status = "[+] " + successMsg;
    else g_Status = T("[-] Ошибка при выполнении: ", "[-] Execution error: ") + successMsg;
}

std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

void DrawSpinner(ImVec2 center, float radius, int thickness, ImU32 color) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float time = (float)ImGui::GetTime();
    window->DrawList->PathClear();
    int num_segments = 30;
    float start = abs(sin(time * 1.8f)) * (IM_PI - 0.5f);
    float min_arc = IM_PI / 8.0f;
    float a_min = IM_PI * 2.0f * ((float)time * 0.8f) + start;
    float a_max = a_min + min_arc + (IM_PI - 0.5f) * abs(cos(time * 1.8f));
    window->DrawList->PathArcTo(center, radius, a_min, a_max, num_segments);
    window->DrawList->PathStroke(color, (float)thickness, 0);
}

std::string OpenFileDialog(bool isFolder) {
    std::string result = "";
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr)) {
        IFileOpenDialog* pFileOpen;
        hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
        if (SUCCEEDED(hr)) {
            DWORD dwOptions;
            if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
                if (isFolder) pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                else pFileOpen->SetOptions(dwOptions | FOS_FORCEFILESYSTEM);
            }
            if (SUCCEEDED(pFileOpen->Show(NULL))) {
                IShellItem* pItem;
                if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                    PWSTR pszFilePath;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                        int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                        std::string strTo(size_needed, 0);
                        WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &strTo[0], size_needed, NULL, NULL);
                        result = strTo;
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }
        CoUninitialize();
    }
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

void ClearDir(const std::string& path) {
    if (!fs::exists(path)) {
        g_Status = T("[-] Путь не найден: ", "[-] Path not found: ") + path;
        return;
    }
    int deleted = 0, failed = 0;
    for (const auto& entry : fs::directory_iterator(path)) {
        try { fs::remove_all(entry.path()); deleted++; } 
        catch (...) { failed++; }
    }
    g_Status = T("[+] Очищено: ", "[+] Cleaned: ") + path + 
               T("\nУдалено: ", "\nDeleted: ") + std::to_string(deleted) + 
               T(" | Пропущено (занято): ", " | Skipped (in use): ") + std::to_string(failed);
}

void ForceDeleteObj(const std::string& path) {
    if (path.empty() || !fs::exists(path)) {
        g_Status = T("[-] Ошибка: Путь не существует или не выбран.", "[-] Error: Path does not exist or is not selected.");
        return;
    }

    g_Status = T("[*] Попытка разблокировки и получения прав...", "[*] Attempting to unlock and acquire rights...");
    std::wstring wpath = Utf8ToWstring(path);

    HMODULE hRm = LoadLibraryA("rstrtmgr.dll");
    if (hRm) {
        auto RmStartSession = (RmStartSession_t)GetProcAddress(hRm, "RmStartSession");
        auto RmRegisterResources = (RmRegisterResources_t)GetProcAddress(hRm, "RmRegisterResources");
        auto RmShutdown = (RmShutdown_t)GetProcAddress(hRm, "RmShutdown");
        auto RmEndSession = (RmEndSession_t)GetProcAddress(hRm, "RmEndSession");

        if (RmStartSession && RmRegisterResources && RmShutdown && RmEndSession) {
            DWORD dwSession;
            WCHAR szSessionKey[CCH_RM_SESSION_KEY + 1] = { 0 };
            if (RmStartSession(&dwSession, 0, szSessionKey) == ERROR_SUCCESS) {
                LPCWSTR rgszResourceNames[] = { wpath.c_str() };
                if (RmRegisterResources(dwSession, 1, rgszResourceNames, 0, NULL, 0, NULL) == ERROR_SUCCESS) {
                    RmShutdown(dwSession, RmForceShutdown, NULL);
                }
                RmEndSession(dwSession);
            }
        }
        FreeLibrary(hRm);
    }

    std::string cmdTakeown = "takeown /f \"" + path + "\" /a /r /d y >nul 2>&1";
    std::string cmdIcacls = "icacls \"" + path + "\" /grant administrators:F /t >nul 2>&1";
    system(cmdTakeown.c_str());
    system(cmdIcacls.c_str());

    SetFileAttributesW(wpath.c_str(), FILE_ATTRIBUTE_NORMAL);

    try {
        fs::remove_all(fs::u8path(path));
        g_Status = T("[+] Удалено: ", "[+] Deleted: ") + path;
        g_SelectedPath = ""; 
    } catch (const fs::filesystem_error& e) {
        g_Status = T("[-] КРИТИЧЕСКАЯ ОШИБКА УДАЛЕНИЯ.\nФайл занят системным ядром.\n", "[-] CRITICAL DELETION ERROR.\nFile is locked by the system kernel.\n") + std::string(e.what());
    }
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

int main() {
    LoadSettings();

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(950, 650, "SysNova", NULL, NULL);
    if (window == NULL) return 1;

    HWND hwnd = glfwGetWin32Window(window);
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101)); 
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
    if (font == NULL) io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f; 
    style.FrameRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);

    ApplyAccentColor(g_AccentColor);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    const char* userProfile = getenv("USERPROFILE");
    const char* systemRoot = getenv("SystemRoot");
    const char* localAppData = getenv("LOCALAPPDATA");
    std::string sysTemp = systemRoot ? std::string(systemRoot) + "\\Temp" : "";
    std::string usrTemp = userProfile ? std::string(userProfile) + "\\AppData\\Local\\Temp" : "";
    std::string downloads = userProfile ? std::string(userProfile) + "\\Downloads" : "";
    std::string prefetch = systemRoot ? std::string(systemRoot) + "\\Prefetch" : "";
    std::vector<std::string> appCaches;
    if (localAppData) {
        appCaches.push_back(std::string(localAppData) + "\\CrashDumps");
        appCaches.push_back(std::string(localAppData) + "\\Microsoft\\Windows\\INetCache");
        appCaches.push_back(std::string(localAppData) + "\\Microsoft\\Windows\\WebCache");
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(2.0f, 5.0f);
    float loadingDuration = dis(gen);
    float fadeDuration = 0.8f;

    bool isDragging = false;
    POINT dragStartPos;
    int windowStartPosX, windowStartPosY;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        float currentTime = (float)glfwGetTime();
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        
        ImGui::Begin("SysNova Main", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        float titleBarHeight = 28.0f;
        
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); 
        ImGui::BeginChild("TitleBar", ImVec2(ImGui::GetWindowWidth(), titleBarHeight), false, ImGuiWindowFlags_NoScrollbar);
        
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            isDragging = true;
            GetCursorPos(&dragStartPos);
            glfwGetWindowPos(window, &windowStartPosX, &windowStartPosY);
        }
        if (isDragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                POINT currentCursorPos;
                GetCursorPos(&currentCursorPos);
                glfwSetWindowPos(window, windowStartPosX + (currentCursorPos.x - dragStartPos.x), windowStartPosY + (currentCursorPos.y - dragStartPos.y));
            } else {
                isDragging = false;
            }
        }

        ImGui::SetCursorPos(ImVec2(15, 4));
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "SysNova");

        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 120, 0)); 
        
        ImVec4 transparentColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparentColor);
        
        if (ImGui::Button("_", ImVec2(60, titleBarHeight))) glfwIconifyWindow(window);
        ImGui::SameLine(0, 0);
        if (ImGui::Button("X", ImVec2(60, titleBarHeight))) glfwSetWindowShouldClose(window, true);
        
        ImGui::PopStyleColor(3); 
        ImGui::EndChild();
        ImGui::PopStyleColor(); 

        ImGui::SetCursorPos(ImVec2(10, titleBarHeight + 5)); 
        ImGui::BeginGroup();

        if (ImGui::BeginTabBar("SysNovaTabs")) {

            if (ImGui::BeginTabItem(T("Очистка от мусора", "Junk Clean"))) {
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                ImGui::Text("%s", T("Удаление временных и ненужных файлов:", "Removing temporary and unnecessary files:"));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                ImGui::PushStyleColor(ImGuiCol_Button, g_AccentColor);
                if (ImGui::Button(T("Очистить ВСЁ", "Clean EVERYTHING"), ImVec2(-1, 50))) {
                    ClearDir(sysTemp);
                    ClearDir(usrTemp);
                    ClearDir(downloads);
                    ClearDir(prefetch);
                    for (const auto& cache : appCaches) ClearDir(cache);
                    SHEmptyRecycleBinA(NULL, NULL, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                    ExecCmd("cleanmgr.exe /d c: /verylowdisk", T("Комплексная очистка завершена.", "Comprehensive cleanup completed."));
                    g_Status = T("[+] Полная очистка системы успешно инициирована!", "[+] Full system cleanup successfully initiated!");
                }
                ImGui::PopStyleColor();

                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                if (ImGui::Button(T("Очистить Temp (Системный)", "Clean Temp (System)"), ImVec2(-1, 35))) ClearDir(sysTemp);
                if (ImGui::Button(T("Очистить %temp% (Пользовательский)", "Clean %temp% (User)"), ImVec2(-1, 35))) ClearDir(usrTemp);
                if (ImGui::Button(T("Очистить папку 'Загрузки'", "Clean 'Downloads' folder"), ImVec2(-1, 35))) ClearDir(downloads);
                if (ImGui::Button(T("Очистить Prefetch", "Clean Prefetch"), ImVec2(-1, 35))) ClearDir(prefetch);
                
                if (ImGui::Button(T("Очистить кэш приложений", "Clean app caches"), ImVec2(-1, 35))) {
                    for (const auto& cache : appCaches) ClearDir(cache);
                    g_Status = T("[+] Кэш браузеров и приложений очищен.", "[+] Browser and app caches cleaned.");
                }

                if (ImGui::Button(T("Очистить Корзину", "Empty Recycle Bin"), ImVec2(-1, 35))) {
                    if (SHEmptyRecycleBinA(NULL, NULL, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND) == S_OK)
                        g_Status = T("[+] Корзина успешно очищена.", "[+] Recycle Bin successfully emptied.");
                }

                if (ImGui::Button(T("Глубокая очистка (Встроенный cleanmgr)", "Deep disk cleanup (Built-in cleanmgr)"), ImVec2(-1, 35))) {
                    g_Status = T("[*] Запущена глубокая системная очистка. Работает в фоне...", "[*] Deep system cleanup started. Running in background...");
                    ExecCmd("cleanmgr.exe /d c: /verylowdisk", T("Системная очистка завершена.", "System cleanup completed."));
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(T("Принудительное удаление", "Force Delete"))) {
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                ImGui::Text("%s", T("МГНОВЕННОЕ удаление (Завершает процессы и забирает права у системы):", 
                                    "INSTANT deletion (Terminates processes and grants system rights):"));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                
                ImGui::Text("%s", T("Выбранный путь:", "Selected path:"));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.16f, 0.21f, 1.0f));
                ImGui::InputText("##PathDisplay", (char*)g_SelectedPath.c_str(), g_SelectedPath.capacity() + 1, ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();

                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                if (ImGui::Button(T("Выбрать ФАЙЛ", "Select FILE"), ImVec2(180, 40))) {
                    std::string res = OpenFileDialog(false);
                    if (!res.empty()) g_SelectedPath = res;
                }
                ImGui::SameLine();
                if (ImGui::Button(T("Выбрать ПАПКУ", "Select FOLDER"), ImVec2(180, 40))) {
                    std::string res = OpenFileDialog(true);
                    if (!res.empty()) g_SelectedPath = res;
                }
                ImGui::SameLine();
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
                if (ImGui::Button(T("Удалить", "Delete"), ImVec2(180, 40))) {
                    ForceDeleteObj(g_SelectedPath);
                }
                ImGui::PopStyleColor(3);
                
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(T("Настройки", "Settings"))) {
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                ImGui::Text("%s", T("Внешний вид:", "Appearance:"));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                if (ImGui::ColorEdit3(T("Цвет акцента (кнопок)", "Accent color (buttons)"), 
                    (float*)&g_AccentColor, 
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) 
                {
                    ApplyAccentColor(g_AccentColor);
                    SaveSettings();
                }

                ImGui::Dummy(ImVec2(0.0f, 20.0f));
                ImGui::Text("%s", T("Язык / Language:", "Язык / Language:"));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                if (ImGui::RadioButton("Русский", &g_Language, 0)) {
                    g_Status = "Язык интерфейса изменен на Русский.";
                    SaveSettings();
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("English", &g_Language, 1)) {
                    g_Status = "Interface language changed to English.";
                    SaveSettings();
                    
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ImGui::EndGroup();

        ImGui::SetCursorPos(ImVec2(10, ImGui::GetWindowHeight() - 70));
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s", T("Статус выполнения:", "Execution status:"));
        ImGui::TextWrapped("%s", g_Status.c_str());

        ImGui::End();

        if (currentTime < loadingDuration + fadeDuration) {
            float alpha = 1.0f;
            if (currentTime > loadingDuration) {
                alpha = 1.0f - ((currentTime - loadingDuration) / fadeDuration);
                if (alpha < 0.0f) alpha = 0.0f;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("LoadingOverlay", nullptr, 
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                ImGuiWindowFlags_NoInputs); 

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(20, 23, 28, 255), 12.0f);

            ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f - 20.0f);
            DrawSpinner(center, 30.0f, 4, IM_COL32(100, 150, 250, 255));

            std::string loadText = T("Инициализация модулей SysNova...", "Initializing SysNova modules...");
            ImVec2 textSize = ImGui::CalcTextSize(loadText.c_str());
            draw_list->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y + 50.0f), IM_COL32(200, 200, 200, 255), loadText.c_str());

            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}