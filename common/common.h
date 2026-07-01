#ifndef COMMON_COMMON_H
#define COMMON_COMMON_H
#include "SharedBuffer.h"
#include "interface_generated.h"
#include <string>

// const std::wstring WINDOW_TITLE = L"*Untitled - Notepad";
const std::string BUFFER_NAME_TX = "SharedBufferTx";
const std::string BUFFER_NAME_RX = "SharedBufferRx";
constexpr std::size_t BUFFER_CAPACITY = 128 * 1024 * 1024;
constexpr std::size_t IPC_MESSAGE_RESERVE = 1024;
constexpr std::size_t REGION_READ_METADATA_RESERVE = 1024 * 1024;
constexpr std::size_t MAX_READ_SIZE = BUFFER_CAPACITY - IPC_MESSAGE_RESERVE;
constexpr std::size_t MAX_REGION_READ_SIZE = BUFFER_CAPACITY - REGION_READ_METADATA_RESERVE;

#endif // COMMON_COMMON_H
