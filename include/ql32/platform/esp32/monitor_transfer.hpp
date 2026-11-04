#pragma once

#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"

#include <Arduino.h>
#include <string_view>

namespace ql32::platform::esp32 {

// Returns true when the line was a RECV or SEND monitor command.
bool handleMonitorTransferCommand(std::string_view line,
                                  runtime::StorageService& storage,
                                  runtime::ChannelService& channels,
                                  HardwareSerial& serial);

} // namespace ql32::platform::esp32
