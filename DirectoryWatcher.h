#pragma once // Используем pragma once в .h файлах

// --- Начало секции DirectoryWatcher (можно вынести в DirectoryWatcher.h/cpp) ---
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread> // Для std::this_thread::sleep_for
#include <chrono> // Для std::chrono::milliseconds

// Используем псевдоним для функции обратного вызова
using LogCallback = std::function<void(const std::wstring&)>;

class DirectoryWatcher {
private:
    HANDLE hDir_ = INVALID_HANDLE_VALUE;
    std::wstring directoryPath_;
    LogCallback logCallback_;
    std::atomic<bool> stopRequested_{ false };
    OVERLAPPED overlapped_{}; // Нужна для ReadDirectoryChangesW
    HANDLE hStopEvent_ = NULL; // Событие для прерывания ожидания

public:
    // Конструктор: получает путь и колбэк для логов
    DirectoryWatcher(const std::wstring& path, LogCallback callback)
        : directoryPath_(path), logCallback_(std::move(callback))
    {
        // Создаем событие для возможности прерывания ReadDirectoryChangesW
        hStopEvent_ = CreateEvent(NULL, TRUE, FALSE, NULL); // Ручной сброс, несигнальное состояние
        if (hStopEvent_ == NULL) {
            // Обработка ошибки создания события (можно бросить исключение)
            logCallback_(L"Ошибка: Не удалось создать событие остановки.");
            return; // или throw std::runtime_error("Failed to create stop event");
        }

        hDir_ = CreateFileW(
            directoryPath_.c_str(),
            FILE_LIST_DIRECTORY,                // Права доступа
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, // Разделение доступа
            NULL,                               // Атрибуты безопасности
            OPEN_EXISTING,                      // Открыть существующую
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, // Флаги (OVERLAPPED обязателен)
            NULL                                // Шаблон файла
        );

        if (hDir_ == INVALID_HANDLE_VALUE) {
            logCallback_(L"Ошибка: Не удалось открыть директорию " + directoryPath_ + L". Код ошибки: " + std::to_wstring(GetLastError()));
            CloseHandle(hStopEvent_); // Очищаем созданное событие
            hStopEvent_ = NULL;
            // Можно бросить исключение
            // throw std::runtime_error("Failed to open directory");
        }
        else {
            logCallback_(L"Наблюдение за директорией " + directoryPath_ + L" начато...");
            // Инициализируем OVERLAPPED
            overlapped_.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL); // Автосброс, несигнальное
            if (overlapped_.hEvent == NULL) {
                logCallback_(L"Ошибка: Не удалось создать событие OVERLAPPED.");
                CloseHandle(hDir_);
                hDir_ = INVALID_HANDLE_VALUE;
                CloseHandle(hStopEvent_);
                hStopEvent_ = NULL;
                // throw std::runtime_error("Failed to create overlapped event");
            }
        }
    }

    // Деструктор: освобождает ресурсы
    ~DirectoryWatcher() {
        logCallback_(L"Остановка наблюдения и освобождение ресурсов для " + directoryPath_ + L"...");
        RequestStop(); // Гарантируем, что флаг установлен

        if (hDir_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(hDir_, &overlapped_); // Отменяем ожидающие операции ввода-вывода
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
        logCallback_(L"Ресурсы наблюдения освобождены.");
    }

    // Запрещаем копирование и присваивание
    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;
    // Разрешаем перемещение (для использования в SmartPtr)
    DirectoryWatcher(DirectoryWatcher&& other) noexcept
        : hDir_(other.hDir_), directoryPath_(std::move(other.directoryPath_)),
        logCallback_(std::move(other.logCallback_)), stopRequested_(other.stopRequested_.load()),
        overlapped_(other.overlapped_), hStopEvent_(other.hStopEvent_)
    {
        other.hDir_ = INVALID_HANDLE_VALUE;
        other.overlapped_.hEvent = NULL;
        other.hStopEvent_ = NULL;
        other.stopRequested_ = true; // Старый объект больше не должен работать
    }
    DirectoryWatcher& operator=(DirectoryWatcher&& other) noexcept {
        if (this != &other) {
            // Освобождаем свои ресурсы перед перемещением
            RequestStop();
            if (hDir_ != INVALID_HANDLE_VALUE) {
                CancelIoEx(hDir_, &overlapped_);
                CloseHandle(hDir_);
            }
            if (overlapped_.hEvent != NULL) CloseHandle(overlapped_.hEvent);
            if (hStopEvent_ != NULL) CloseHandle(hStopEvent_);

            // Перемещаем данные
            hDir_ = other.hDir_;
            directoryPath_ = std::move(other.directoryPath_);
            logCallback_ = std::move(other.logCallback_);
            stopRequested_ = other.stopRequested_.load();
            overlapped_ = other.overlapped_;
            hStopEvent_ = other.hStopEvent_;

            // Обнуляем старый объект
            other.hDir_ = INVALID_HANDLE_VALUE;
            other.overlapped_.hEvent = NULL;
            other.hStopEvent_ = NULL;
            other.stopRequested_ = true;
        }
        return *this;
    }


    // Метод, выполняющийся в отдельном потоке
    void MonitorLoop() {
        if (hDir_ == INVALID_HANDLE_VALUE || overlapped_.hEvent == NULL || hStopEvent_ == NULL) {
            logCallback_(L"Ошибка: Наблюдатель не инициализирован должным образом.");
            return;
        }

        const DWORD bufferSize = 4096; // Размер буфера для уведомлений
        // Используем вектор для автоматического управления памятью буфера
        std::vector<BYTE> buffer(bufferSize);
        DWORD bytesReturned = 0;

        // Флаги отслеживаемых изменений
        DWORD notifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | // Создание, удаление, переименование файлов
            FILE_NOTIFY_CHANGE_DIR_NAME | // Создание, удаление, переименование папок
            FILE_NOTIFY_CHANGE_LAST_WRITE; // Изменения в файлах (можно убрать, если не нужно)
        // FILE_NOTIFY_CHANGE_CREATION - можно добавить для времени создания

        while (!stopRequested_) {
            BOOL success = ReadDirectoryChangesW(
                hDir_,                           // Хэндл директории
                buffer.data(),                   // Буфер для результатов
                buffer.size(),                   // Размер буфера
                TRUE,                            // Наблюдать за поддиректориями? (TRUE да)
                notifyFilter,                    // Типы изменений для отслеживания
                &bytesReturned,                  // Количество байт, записанных в буфер (не используется с OVERLAPPED)
                &overlapped_,                    // Структура OVERLAPPED
                NULL                             // Функция завершения (не используется здесь)
            );

            if (!success) {
                DWORD error = GetLastError();
                if (error == ERROR_OPERATION_ABORTED) {
                    logCallback_(L"Операция наблюдения прервана.");
                    break; // Выходим из цикла по запросу остановки
                }
                else {
                    logCallback_(L"Ошибка ReadDirectoryChangesW: " + std::to_wstring(error));
                    // Можно добавить задержку перед повторной попыткой или выйти
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }

            // Ожидаем завершения операции или сигнала остановки
            HANDLE handles[] = { overlapped_.hEvent, hStopEvent_ };
            DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE); // Ждем любое из событий

            if (stopRequested_) { // Проверяем снова после ожидания
                logCallback_(L"Получен сигнал остановки во время ожидания.");
                break;
            }

            if (waitResult == WAIT_OBJECT_0) { // Событие от ReadDirectoryChangesW
                // Получаем результат асинхронной операции
                DWORD bytesTransferred = 0;
                if (!GetOverlappedResult(hDir_, &overlapped_, &bytesTransferred, FALSE)) {
                    DWORD error = GetLastError();
                    if (error == ERROR_OPERATION_ABORTED) {
                        logCallback_(L"Операция GetOverlappedResult прервана.");
                        break;
                    }
                    logCallback_(L"Ошибка GetOverlappedResult: " + std::to_wstring(error));
                    continue; // Продолжаем цикл, если ошибка не критична
                }

                if (bytesTransferred == 0) {
                    // Буфер может быть слишком мал, хотя 4кб обычно достаточно
                    logCallback_(L"Предупреждение: Получено 0 байт от ReadDirectoryChangesW (возможно, буфер мал?).");
                    continue;
                }

                // Обрабатываем уведомления в буфере
                ProcessNotifications(buffer.data(), bytesTransferred);

            }
            else if (waitResult == WAIT_OBJECT_0 + 1) { // Сработало событие hStopEvent_
                logCallback_(L"Получен внешний сигнал остановки.");
                break; // Выходим из цикла
            }
            else {
                // Ошибка ожидания
                logCallback_(L"Ошибка WaitForMultipleObjects: " + std::to_wstring(GetLastError()));
                break;
            }
        } // end while

        logCallback_(L"Цикл наблюдения завершен.");
    }

    // Запрос на остановку мониторинга
    void RequestStop() {
        stopRequested_ = true;
        // Сигналим событие, чтобы прервать WaitForMultipleObjects, если он ожидает
        if (hStopEvent_ != NULL) {
            SetEvent(hStopEvent_);
        }
        // Также отменяем IO операцию, если она выполняется
        if (hDir_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(hDir_, &overlapped_);
        }
    }

private:
    // Обработка полученных уведомлений
    void ProcessNotifications(BYTE* pBuffer, DWORD dwBytesReturned) {
        FILE_NOTIFY_INFORMATION* pNotifyInfo = (FILE_NOTIFY_INFORMATION*)pBuffer;
        DWORD offset = 0;
        std::wstring oldName = L""; // Для хранения старого имени при переименовании

        do {
            // Получаем имя файла/папки
            // FileNameLength измеряется в БАЙТАХ, не символах!
            std::wstring fileName(pNotifyInfo->FileName, pNotifyInfo->FileNameLength / sizeof(wchar_t));

            // Формируем сообщение лога
            std::wstring logMessage;
            switch (pNotifyInfo->Action) {
            case FILE_ACTION_ADDED:
                // Сложно точно определить, файл или папка, без доп. запроса к ФС
                // Будем считать папкой, если нет расширения (упрощение)
                if (fileName.find('.') == std::wstring::npos) {
                    logMessage = L"Создана папка \"" + fileName + L"\"";
                }
                else {
                    logMessage = L"Создан файл \"" + fileName + L"\"";
                }
                break;
            case FILE_ACTION_REMOVED:
                // По ТЗ не требуется, но можно добавить
                // logMessage = L"Удален \"" + fileName + L"\"";
                break;
            case FILE_ACTION_MODIFIED:
                // По ТЗ не требуется
                // logMessage = L"Изменен \"" + fileName + L"\"";
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                oldName = fileName; // Сохраняем старое имя
                break; // Ничего не логируем, ждем RENAMED_NEW_NAME
            case FILE_ACTION_RENAMED_NEW_NAME:
                if (!oldName.empty()) {
                    logMessage = L"Файл/папка \"" + oldName + L"\" переименован(а) на \"" + fileName + L"\"";
                    oldName.clear(); // Сбрасываем старое имя
                }
                else {
                    // Ситуация, когда пришло NEW_NAME без OLD_NAME (редко, но возможно)
                    logMessage = L"Переименовано на \"" + fileName + L"\" (старое имя не зафиксировано)";
                }
                break;
            default:
                logMessage = L"Неизвестное действие (" + std::to_wstring(pNotifyInfo->Action) + L") для \"" + fileName + L"\"";
                break;
            }

            if (!logMessage.empty()) {
                logCallback_(logMessage); // Вызываем колбэк с сообщением
            }

            // Переходим к следующей записи, если она есть
            offset = pNotifyInfo->NextEntryOffset;
            pNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pNotifyInfo + offset);

        } while (offset != 0 && !stopRequested_); // Проверяем stopRequested_ и здесь
    }
};
// --- Конец секции DirectoryWatcher ---