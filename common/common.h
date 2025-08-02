#ifndef COMMON_COMMON_H
#define COMMON_COMMON_H
#include <string>
#include "shared_buffer.hpp"
#include "interface_generated.h"

// const std::wstring WINDOW_TITLE = L"*Untitled - Notepad";
const std::wstring WINDOW_TITLE = L"Lineage2M l Katzman";
const std::string BUFFER_NAME_TX = "SharedBufferTx";
const std::string BUFFER_NAME_RX = "SharedBufferRx";
const std::size_t BUFFER_CAPACITY = 128 * 1024 * 1024;

#endif // COMMON_COMMON_H
