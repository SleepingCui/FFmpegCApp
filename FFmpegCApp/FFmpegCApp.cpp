#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <chrono>
#include <iomanip>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

using namespace std;
namespace fs = std::filesystem;
using namespace std::chrono;

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"

string wstringToUtf8(const wstring& wstr) {
    if (wstr.empty()) return string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), result.data(), size_needed, nullptr, nullptr);
    return result;
}
void replaceAll(string& str, const string& from, const string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}
wstring selectFolder() {
    wchar_t path[MAX_PATH];
    BROWSEINFOW bi = { 0 };
    bi.lpszTitle = L"Select the folder containing video files (unsupports UTF-8)";
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        SHGetPathFromIDListW(pidl, path);
        return wstring(path);
    }
    return L"";
}
void runCommand(const string& cmd, const string& title) {
    cout << "[DEBUG] Entered runCommand" << endl;
    cout << COLOR_YELLOW << "[RUN] " << cmd << COLOR_RESET << endl;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    string fullCmd = "cmd.exe /c title " + title + " && " + cmd + " && exit";
    string cmdCopy = fullCmd;
    char* cmdLine = cmdCopy.data();
    BOOL success = CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE,
        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

    if (!success) {
        cerr << COLOR_RED << "[ERROR] Failed to start process: " << GetLastError() << COLOR_RESET << endl;
    }
    else {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        if (exitCode != 0) {
            cerr << COLOR_RED << "[ERROR] Command exited with code: " << exitCode << COLOR_RESET << endl;
        }
        else {
            cout << COLOR_GREEN << "[OK] Command finished successfully." << COLOR_RESET << endl;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}
vector<string> loadCommands(const fs::path& exeDir) {
    fs::path cmdFile = exeDir / "commands.txt";
    vector<string> commands;

    if (!fs::exists(cmdFile)) {
        ofstream ofs(cmdFile);
        ofs << "# Command template, use input / temp / output placeholders\n";
        ofs << "ffmpeg -y -i input -vf \"minterpolate=fps=60:mi_mode=blend\" -preset fast temp\n";
        ofs << "ffmpeg -y -i temp -vf \"tblend=all_mode=average\" -r 120 output\n";
        ofs.close();
        cout << COLOR_YELLOW << "[INFO] Created default commands.txt" << COLOR_RESET << endl;
    }

    ifstream ifs(cmdFile);
    string line;
    while (getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        commands.push_back(line);
    }
    cout << "[DEBUG] Loaded " << commands.size() << " command(s) from commands.txt" << endl;
    return commands;
}
int main() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    fs::path exeDir = fs::path(exePath);
    cout << "[DEBUG] Executable directory: " << wstringToUtf8(exeDir.wstring()) << endl;
    auto commands = loadCommands(exeDir);
    if (commands.empty()) {
        cerr << COLOR_RED << "[ERROR] No commands found in commands.txt." << COLOR_RESET << endl;
        return 1;
    }
    wstring folderW = selectFolder();
    if (folderW.empty()) {
        wcerr << COLOR_RED << L"[ERROR] No folder selected. Exiting." << COLOR_RESET << endl;
        return 1;
    }
    fs::path folder(folderW);
    cout << "[DEBUG] Selected folder: " << wstringToUtf8(folder.wstring()) << endl;

    int fileCount = 0;
    auto start = high_resolution_clock::now();
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().wstring();
            for (auto& ch : ext) ch = towlower(ch);
            wcout << L"[DEBUG] Found file: " << entry.path().filename() << endl;
            if (ext == L".mp4" || ext == L".mkv") {
                fileCount++;
                auto input = entry.path();
                auto stem = input.stem().wstring();
                auto temp = folder / (stem + L"_temp.mp4");
                auto output = folder / (stem + L"_out.mp4");

                string inputUtf8 = "\"" + wstringToUtf8(input.wstring()) + "\"";
                string tempUtf8 = "\"" + wstringToUtf8(temp.wstring()) + "\"";
                string outputUtf8 = "\"" + wstringToUtf8(output.wstring()) + "\"";

                wcout << COLOR_YELLOW << L"[INFO] Processing: " << input.filename().wstring() << COLOR_RESET << endl;
                string title = "\"Processing " + wstringToUtf8(input.filename().wstring()) + "\"";

                for (const auto& cmdTemplate : commands) {
                    cout << "[DEBUG] Original command: " << cmdTemplate << endl;
                    string cmd = cmdTemplate;
                    replaceAll(cmd, "input", inputUtf8);
                    replaceAll(cmd, "temp", tempUtf8);
                    replaceAll(cmd, "output", outputUtf8);
                    cout << "[DEBUG] Final command: " << cmd << endl;
                    runCommand(cmd, title);
                }
                 if (fs::exists(temp)) {
                    cout << "[DEBUG] Removing temp file: " << tempUtf8 << endl;
                    fs::remove(temp);
                }
            }
        }
    }
    auto end = high_resolution_clock::now();
    duration<double> elapsed = end - start;
    cout << COLOR_GREEN << "[DONE] Processed " << fileCount << " file(s) in "
        << fixed << setprecision(1) << elapsed.count() << "s." << COLOR_RESET << endl;

    return 0;
}
