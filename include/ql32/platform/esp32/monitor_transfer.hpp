#pragma once

#include "ql32/runtime/channel.hpp"
#include "ql32/runtime/storage.hpp"
#include "ql32/runtime/ymodem.hpp"

#include <Arduino.h>
#include <functional>
#include <string_view>

namespace ql32::platform::esp32 {

// Returns true when the line was a RECV, SEND, or SCREEN monitor command.
bool handleMonitorTransferCommand(std::string_view line,
                                  runtime::StorageService& storage,
                                  runtime::ChannelService& channels,
                                  HardwareSerial& serial,
                                  const std::function<runtime::YModemSource()>&
                                      screenCapture = {});

} // namespace ql32::platform::esp32
