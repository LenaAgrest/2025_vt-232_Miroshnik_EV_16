#include "MyForm.h" // Подключаем заголовок нашей формы

using namespace System;
using namespace System::Windows::Forms;
using namespace My2025vt232MiroshnikEV16; // Используем наше пространство имен

[STAThreadAttribute] // Атрибут для приложений Windows Forms
int main(array<String^>^ args) {
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);

    // Создаем и запускаем нашу форму
    MyForm^ form = gcnew MyForm();
    Application::Run(form);

    return 0;
}