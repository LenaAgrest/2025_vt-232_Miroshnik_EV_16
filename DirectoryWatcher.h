#pragma once // ���������� pragma once � .h ������

// --- ������ ������ DirectoryWatcher (����� ������� � DirectoryWatcher.h/cpp) ---
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread> // ��� std::this_thread::sleep_for
#include <chrono> // ��� std::chrono::milliseconds

// ���������� ��������� ��� ������� ��������� ������
using LogCallback = std::function<void(const std::wstring&)>;

class DirectoryWatcher {
private:
    HANDLE hDir_ = INVALID_HANDLE_VALUE;
    std::wstring directoryPath_;
    LogCallback logCallback_;
    std::atomic<bool> stopRequested_{ false };
    OVERLAPPED overlapped_{}; // ����� ��� ReadDirectoryChangesW
    HANDLE hStopEvent_ = NULL; // ������� ��� ���������� ��������

public:
    // �����������: �������� ���� � ������ ��� �����
    DirectoryWatcher(const std::wstring& path, LogCallback callback)
        : directoryPath_(path), logCallback_(std::move(callback))
    {
        // ������� ������� ��� ����������� ���������� ReadDirectoryChangesW
        hStopEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL); // ������ �����, ������������ ���������
        if (hStopEvent_ == NULL) {
            // ��������� ������ �������� ������� (����� ������� ����������)
            logCallback_(L"������: �� ������� ������� ������� ���������.");
            return; // ��� throw std::runtime_error("Failed to create stop event");
        }

        hDir_ = CreateFileW(
            directoryPath_.c_str(),
            FILE_LIST_DIRECTORY,                // ����� �������
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, // ���������� �������
            NULL,                               // �������� ������������
            OPEN_EXISTING,                      // ������� ������������
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, // ����� (OVERLAPPED ����������)
            NULL                                // ������ �����
        );

        if (hDir_ == INVALID_HANDLE_VALUE) {
            logCallback_(L"������: �� ������� ������� ���������� " + directoryPath_ + L". ��� ������: " + std::to_wstring(GetLastError()));
            CloseHandle(hStopEvent_); // ������� ��������� �������
            hStopEvent_ = NULL;
            // ����� ������� ����������
            // throw std::runtime_error("Failed to open directory");
        }
        else {
            logCallback_(L"���������� �� ����������� " + directoryPath_ + L" ������...");
            // �������������� OVERLAPPED
            overlapped_.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL); // ���������, ������������
            if (overlapped_.hEvent == NULL) {
                logCallback_(L"������: �� ������� ������� ������� OVERLAPPED.");
                CloseHandle(hDir_);
                hDir_ = INVALID_HANDLE_VALUE;
                CloseHandle(hStopEvent_);
                hStopEvent_ = NULL;
                // throw std::runtime_error("Failed to create overlapped event");
            }
        }
    }

    // ����������: ����������� �������
    ~DirectoryWatcher() {
        logCallback_(L"��������� ���������� � ������������ �������� ��� " + directoryPath_ + L"...");
        RequestStop(); // �����������, ��� ���� ����������

        if (hDir_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(hDir_, &overlapped_); // �������� ��������� �������� �����-������
            CloseHandle(hDir_);
            hDir_ = INVALID_HANDLE_VALUE;
        }
        if (overlapped_.hEvent != NULL) {
            CloseHandle(overlapped_.hEvent);
            overlapped_.hEvent = NULL;
        }
        if (hStopEvent_ != NULL) {
            CloseHandle(hStopEvent_);
            hStopEvent_ = NULL;
        }
        logCallback_(L"������� ���������� �����������.");
    }

    // ��������� ����������� � ������������
    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;
    // ��������� ����������� (��� ������������� � SmartPtr)
    DirectoryWatcher(DirectoryWatcher&& other) noexcept
        : hDir_(other.hDir_), directoryPath_(std::move(other.directoryPath_)),
        logCallback_(std::move(other.logCallback_)), stopRequested_(other.stopRequested_.load()),
        overlapped_(other.overlapped_), hStopEvent_(other.hStopEvent_)
    {
        other.hDir_ = INVALID_HANDLE_VALUE;
        other.overlapped_.hEvent = NULL;
        other.hStopEvent_ = NULL;
        other.stopRequested_ = true; // ������ ������ ������ �� ������ ��������
    }
    DirectoryWatcher& operator=(DirectoryWatcher&& other) noexcept {
        if (this != &other) {
            // ����������� ���� ������� ����� ������������
            RequestStop();
            if (hDir_ != INVALID_HANDLE_VALUE) {
                CancelIoEx(hDir_, &overlapped_);
                CloseHandle(hDir_);
            }
            if (overlapped_.hEvent != NULL) CloseHandle(overlapped_.hEvent);
            if (hStopEvent_ != NULL) CloseHandle(hStopEvent_);

            // ���������� ������
            hDir_ = other.hDir_;
            directoryPath_ = std::move(other.directoryPath_);
            logCallback_ = std::move(other.logCallback_);
            stopRequested_ = other.stopRequested_.load();
            overlapped_ = other.overlapped_;
            hStopEvent_ = other.hStopEvent_;

            // �������� ������ ������
            other.hDir_ = INVALID_HANDLE_VALUE;
            other.overlapped_.hEvent = NULL;
            other.hStopEvent_ = NULL;
            other.stopRequested_ = true;
        }
        return *this;
    }


    // �����, ������������� � ��������� ������
    void MonitorLoop() {
        if (hDir_ == INVALID_HANDLE_VALUE || overlapped_.hEvent == NULL || hStopEvent_ == NULL) {
            logCallback_(L"������: ����������� �� ��������������� ������� �������.");
            return;
        }

        const DWORD bufferSize = 4096; // ������ ������ ��� �����������
        // ���������� ������ ��� ��������������� ���������� ������� ������
        std::vector<BYTE> buffer(bufferSize);
        DWORD bytesReturned = 0;

        // ����� ������������� ���������
        DWORD notifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | // ��������, ��������, �������������� ������
            FILE_NOTIFY_CHANGE_DIR_NAME | // ��������, ��������, �������������� �����
            FILE_NOTIFY_CHANGE_LAST_WRITE; // ��������� � ������ (����� ������, ���� �� �����)
        // FILE_NOTIFY_CHANGE_CREATION - ����� �������� ��� ������� ��������

        while (!stopRequested_) {
            BOOL success = ReadDirectoryChangesW(
                hDir_,                           // ����� ����������
                buffer.data(),                   // ����� ��� �����������
                buffer.size(),                   // ������ ������
                TRUE,                            // ��������� �� ���������������? (TRUE ��)
                notifyFilter,                    // ���� ��������� ��� ������������
                &bytesReturned,                  // ���������� ����, ���������� � ����� (�� ������������ � OVERLAPPED)
                &overlapped_,                    // ��������� OVERLAPPED
                NULL                             // ������� ���������� (�� ������������ �����)
            );

            if (!success) {
                DWORD error = GetLastError();
                if (error == ERROR_OPERATION_ABORTED) {
                    logCallback_(L"�������� ���������� ��������.");
                    break; // ������� �� ����� �� ������� ���������
                }
                else {
                    logCallback_(L"������ ReadDirectoryChangesW: " + std::to_wstring(error));
                    // ����� �������� �������� ����� ��������� �������� ��� �����
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }

            // ������� ���������� �������� ��� ������� ���������
            HANDLE handles[] = { overlapped_.hEvent, hStopEvent_ };
            DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE); // ���� ����� �� �������

            if (stopRequested_) { // ��������� ����� ����� ��������
                logCallback_(L"������� ������ ��������� �� ����� ��������.");
                break;
            }

            if (waitResult == WAIT_OBJECT_0) { // ������� �� ReadDirectoryChangesW
                // �������� ��������� ����������� ��������
                DWORD bytesTransferred = 0;
                if (!GetOverlappedResult(hDir_, &overlapped_, &bytesTransferred, FALSE)) {
                    DWORD error = GetLastError();
                    if (error == ERROR_OPERATION_ABORTED) {
                        logCallback_(L"�������� GetOverlappedResult ��������.");
                        break;
                    }
                    logCallback_(L"������ GetOverlappedResult: " + std::to_wstring(error));
                    continue; // ���������� ����, ���� ������ �� ��������
                }

                if (bytesTransferred == 0) {
                    // ����� ����� ���� ������� ���, ���� 4�� ������ ����������
                    logCallback_(L"��������������: �������� 0 ���� �� ReadDirectoryChangesW (��������, ����� ���?).");
                    continue;
                }

                // ������������ ����������� � ������
                ProcessNotifications(buffer.data(), bytesTransferred);

            }
            else if (waitResult == WAIT_OBJECT_0 + 1) { // ��������� ������� hStopEvent_
                logCallback_(L"������� ������� ������ ���������.");
                break; // ������� �� �����
            }
            else {
                // ������ ��������
                logCallback_(L"������ WaitForMultipleObjects: " + std::to_wstring(GetLastError()));
                break;
            }
        } // end while

        logCallback_(L"���� ���������� ��������.");
    }

    // ������ �� ��������� �����������
    void RequestStop() {
        stopRequested_ = true;
        // �������� �������, ����� �������� WaitForMultipleObjects, ���� �� �������
        if (hStopEvent_ != NULL) {
            SetEvent(hStopEvent_);
        }
        // ����� �������� IO ��������, ���� ��� �����������
        if (hDir_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(hDir_, &overlapped_);
        }
    }

private:
    // ��������� ���������� �����������
    void ProcessNotifications(BYTE* pBuffer, DWORD dwBytesReturned) {
        FILE_NOTIFY_INFORMATION* pNotifyInfo = (FILE_NOTIFY_INFORMATION*)pBuffer;
        DWORD offset = 0;
        std::wstring oldName = L""; // ��� �������� ������� ����� ��� ��������������

        do {
            // �������� ��� �����/�����
            // FileNameLength ���������� � ������, �� ��������!
            std::wstring fileName(pNotifyInfo->FileName, pNotifyInfo->FileNameLength / sizeof(wchar_t));

            // ��������� ��������� ����
            std::wstring logMessage;
            switch (pNotifyInfo->Action) {
            case FILE_ACTION_ADDED:
                // ������ ����� ����������, ���� ��� �����, ��� ���. ������� � ��
                // ����� ������� ������, ���� ��� ���������� (���������)
                if (fileName.find('.') == std::wstring::npos) {
                    logMessage = L"������� ����� \"" + fileName + L"\"";
                }
                else {
                    logMessage = L"������ ���� \"" + fileName + L"\"";
                }
                break;
            case FILE_ACTION_REMOVED:
                // �� �� �� ���������, �� ����� ��������
                // logMessage = L"������ \"" + fileName + L"\"";
                break;
            case FILE_ACTION_MODIFIED:
                // �� �� �� ���������
                // logMessage = L"������� \"" + fileName + L"\"";
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                oldName = fileName; // ��������� ������ ���
                break; // ������ �� ��������, ���� RENAMED_NEW_NAME
            case FILE_ACTION_RENAMED_NEW_NAME:
                if (!oldName.empty()) {
                    logMessage = L"����/����� \"" + oldName + L"\" ������������(�) �� \"" + fileName + L"\"";
                    oldName.clear(); // ���������� ������ ���
                }
                else {
                    // ��������, ����� ������ NEW_NAME ��� OLD_NAME (�����, �� ��������)
                    logMessage = L"������������� �� \"" + fileName + L"\" (������ ��� �� �������������)";
                }
                break;
            default:
                logMessage = L"����������� �������� (" + std::to_wstring(pNotifyInfo->Action) + L") ��� \"" + fileName + L"\"";
                break;
            }

            if (!logMessage.empty()) {
                logCallback_(logMessage); // �������� ������ � ����������
            }

            // ��������� � ��������� ������, ���� ��� ����
            offset = pNotifyInfo->NextEntryOffset;
            pNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pNotifyInfo + offset);

        } while (offset != 0 && !stopRequested_); // ��������� stopRequested_ � �����
    }
};
// --- ����� ������ DirectoryWatcher ---