#pragma once
#include <string>
#include <fstream>
#include <ctime>
#include <Windows.h>
#include "SmartPointer.h"
#include <filesystem>

class Logger {
private:
    std::ofstream logFile;
    std::string path;

    std::string getCurrentTimestamp() {
        time_t now = time(0);
        tm local;
        localtime_s(&local, &now);
        char buf[80];
        strftime(buf, sizeof(buf), "[%d:%m:%Y %H:%M:%S] ", &local);
        return std::string(buf);
    }

public:
    Logger(const std::string& /*filename*/) {
        std::string exePath = std::filesystem::current_path().string(); // путь к .exe
        path = exePath + "\\log.txt"; // лог в папке проекта

        logFile.open(path, std::ios::out | std::ios::app);
        if (logFile.is_open()) {
            logFile << "Наблюдение начато...\n";
        }
    }

    ~Logger() {
        if (logFile.is_open()) {
            logFile << "Наблюдение остановлено...\n";
            logFile.close();
        }
    }

    void Log(const std::string& message) {
        if (logFile.is_open()) {
            logFile << getCurrentTimestamp() << message << "\"\n";
        }
    }

    void LogRaw(const std::string& raw) {
        if (logFile.is_open()) {
            logFile << raw << "\n";
        }
    }
};
