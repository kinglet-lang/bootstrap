// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#pragma once

#include "driver/preen/config.h"

#include <string>

namespace kinglet::preen {

std::string apply_extensions(const std::string &formatted, const FmtConfig &config);

} // namespace kinglet::preen
