#pragma once

#include "Logger.h"
#include "SmartPointer.h"
#include <string>
#include <fstream>
#include <ctime>
#include <Windows.h>
#include "msclr/marshal_cppstd.h"

namespace My2025vt232MiroshnikEV16 {

	using namespace System;
	using namespace System::Windows::Forms;
	using namespace System::IO;

	public ref class MyForm : public Form
	{
	private:
		FileSystemWatcher^ watcher;
		String^ currentPath;
		SmartPointer<Logger>* logger;

	public:
		MyForm(void) {
			InitializeComponent();
			watcher = gcnew FileSystemWatcher();
			logger = nullptr;
		}

	protected:
		~MyForm() {
			if (logger != nullptr) {
				delete logger;
			}
		}

	private: System::Windows::Forms::TextBox^ logBox;
	private: System::Windows::Forms::Button^ chooseFolderBtn;
	private: System::Windows::Forms::Button^ startButton;
	private: System::Windows::Forms::Button^ stopButton;

#pragma region Windows Form Designer generated code
		   void InitializeComponent(void)
		   {
			   this->logBox = (gcnew System::Windows::Forms::TextBox());
			   this->chooseFolderBtn = (gcnew System::Windows::Forms::Button());
			   this->SuspendLayout();
			   // 
			   // logBox
			   // 
			   this->logBox->Location = System::Drawing::Point(12, 51);
			   this->logBox->Multiline = true;
			   this->logBox->Name = L"logBox";
			   this->logBox->ReadOnly = true;
			   this->logBox->ScrollBars = System::Windows::Forms::ScrollBars::Vertical;
			   this->logBox->Size = System::Drawing::Size(560, 297);
			   this->logBox->TabIndex = 0;
			   // 
			   // chooseFolderBtn
			   // 
			   this->chooseFolderBtn->Location = System::Drawing::Point(12, 12);
			   this->chooseFolderBtn->Name = L"chooseFolderBtn";
			   this->chooseFolderBtn->Size = System::Drawing::Size(160, 30);
			   this->chooseFolderBtn->TabIndex = 1;
			   this->chooseFolderBtn->Text = L"Выбрать папку";
			   this->chooseFolderBtn->UseVisualStyleBackColor = true;
			   this->chooseFolderBtn->Click += gcnew System::EventHandler(this, &MyForm::chooseFolderBtn_Click);
			   // 
			   // Form1
			   // 
			   this->ClientSize = System::Drawing::Size(584, 361);
			   this->Controls->Add(this->chooseFolderBtn);
			   this->Controls->Add(this->logBox);
			   this->Name = L"Form1";
			   this->Text = L"Контроль изменений";
			   this->ResumeLayout(false);
			   this->PerformLayout();
			   // 
			   // startButton
			   // 
			   this->startButton = (gcnew System::Windows::Forms::Button());
			   this->startButton->Location = System::Drawing::Point(190, 12);
			   this->startButton->Name = L"startButton";
			   this->startButton->Size = System::Drawing::Size(120, 30);
			   this->startButton->TabIndex = 2;
			   this->startButton->Text = L"Начать";
			   this->startButton->UseVisualStyleBackColor = true;
			   this->startButton->Click += gcnew System::EventHandler(this, &MyForm::startButton_Click);
			   // 
			   // stopButton
			   // 
			   this->stopButton = (gcnew System::Windows::Forms::Button());
			   this->stopButton->Location = System::Drawing::Point(320, 12);
			   this->stopButton->Name = L"stopButton";
			   this->stopButton->Size = System::Drawing::Size(120, 30);
			   this->stopButton->TabIndex = 3;
			   this->stopButton->Text = L"Стоп";
			   this->stopButton->UseVisualStyleBackColor = true;
			   this->stopButton->Click += gcnew System::EventHandler(this, &MyForm::stopButton_Click);

			   // Добавляем на форму
			   this->Controls->Add(this->startButton);
			   this->Controls->Add(this->stopButton);

		   }
#pragma endregion

	private:
		void chooseFolderBtn_Click(System::Object^ sender, System::EventArgs^ e) {
			FolderBrowserDialog^ dialog = gcnew FolderBrowserDialog();
			if (dialog->ShowDialog() == System::Windows::Forms::DialogResult::OK) {
				// Сброс старого логгера
				if (logger != nullptr) {
					delete logger;
					logger = nullptr;
				}

				currentPath = dialog->SelectedPath;
				logBox->Clear();

				// Новый лог
				std::string logFile = msclr::interop::marshal_as<std::string>(currentPath + "\\log.txt");
				logger = new SmartPointer<Logger>(new Logger(logFile));

				//logBox->AppendText("Наблюдение начато...\r\n");

				// Настройка наблюдения
				watcher->Path = currentPath;
				watcher->EnableRaisingEvents = true;
				watcher->IncludeSubdirectories = true;

				watcher->Created += gcnew FileSystemEventHandler(this, &MyForm::OnCreated);
				watcher->Renamed += gcnew RenamedEventHandler(this, &MyForm::OnRenamed);
				watcher->Deleted += gcnew FileSystemEventHandler(this, &MyForm::OnDeleted);
				watcher->Changed += gcnew FileSystemEventHandler(this, &MyForm::OnChanged);

			}
		}

		void startButton_Click(System::Object^ sender, System::EventArgs^ e) {
			if (currentPath == nullptr || currentPath->Length == 0) {
				MessageBox::Show("Сначала выберите папку!");
				return;
			}

			if (!watcher->EnableRaisingEvents) {
				watcher->EnableRaisingEvents = true;
				logBox->AppendText("Наблюдение начато...\r\n");
				if (logger) {
					logger->get()->LogRaw("Наблюдение начато...");
				}
			}
		}

		void stopButton_Click(System::Object^ sender, System::EventArgs^ e) {
			if (watcher->EnableRaisingEvents) {
				watcher->EnableRaisingEvents = false;
				logBox->AppendText("Наблюдение остановлено...\r\n");
				if (logger) {
					logger->get()->LogRaw("Наблюдение остановлено...");
				}
			}
		}

		void OnCreated(Object^ sender, FileSystemEventArgs^ e) {
			String^ type = Directory::Exists(e->FullPath) ? "Создана папка" : "Создан файл";
			String^ msg = String::Format("{0} \"{1}\"", type, e->Name);

			logBox->Invoke(gcnew Action<String^>(this, &MyForm::AppendLog), msg);
			logger->get()->Log(msclr::interop::marshal_as<std::string>(msg));
		}

		void OnRenamed(Object^ sender, RenamedEventArgs^ e) {
			String^ msg = String::Format("Файл \"{0}\" переименован на \"{1}\"", e->OldName, e->Name);
			logBox->Invoke(gcnew Action<String^>(this, &MyForm::AppendLog), msg);
			logger->get()->Log(msclr::interop::marshal_as<std::string>(msg));
		}

		void AppendLog(String^ text) {
			DateTime now = DateTime::Now;
			String^ timestamp = now.ToString("[HH:mm:ss dd:MM:yyyy] ");
			logBox->AppendText(timestamp + text + "\r\n");
		}

		void OnDeleted(Object^ sender, FileSystemEventArgs^ e) {
			String^ type = Directory::Exists(e->FullPath) ? "Удалена папка" : "Удалён файл";
			String^ msg = String::Format("{0} \"{1}\"", type, e->Name);

			logBox->Invoke(gcnew Action<String^>(this, &MyForm::AppendLog), msg);
			if (logger) logger->get()->Log(msclr::interop::marshal_as<std::string>(msg));
		}

		void OnChanged(Object^ sender, FileSystemEventArgs^ e) {
			// Защита от срабатываний на папки
			if (Directory::Exists(e->FullPath)) return;

			String^ msg = String::Format("Изменён файл \"{0}\"", e->Name);

			logBox->Invoke(gcnew Action<String^>(this, &MyForm::AppendLog), msg);
			if (logger) logger->get()->Log(msclr::interop::marshal_as<std::string>(msg));
		}


	};
}
