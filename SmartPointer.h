#pragma once

template<typename T>
class SmartPointer {
private:
    T* ptr;

public:
    // Конструктор
    explicit SmartPointer(T* p = nullptr) : ptr(p) {}

    // Деструктор
    ~SmartPointer() {
        delete ptr;
    }

    // Запрет копирования
    SmartPointer(const SmartPointer&) = delete;
    SmartPointer& operator=(const SmartPointer&) = delete;

    // Разрешение перемещения
    SmartPointer(SmartPointer&& other) noexcept {
        ptr = other.ptr;
        other.ptr = nullptr;
    }

    SmartPointer& operator=(SmartPointer&& other) noexcept {
        if (this != &other) {
            delete ptr;
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    // Доступ к объекту
    T& operator*() const { return *ptr; }
    T* operator->() const { return ptr; }

    // Получить внутренний указатель
    T* get() const { return ptr; }

    // Освободить объект вручную
    void reset(T* p = nullptr) {
        if (ptr != p) {
            delete ptr;
            ptr = p;
        }
    }

    // Освободить владение (без удаления)
    T* release() {
        T* oldPtr = ptr;
        ptr = nullptr;
        return oldPtr;
    }
};
