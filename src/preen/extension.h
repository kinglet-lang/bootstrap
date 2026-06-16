#pragma once

#include "preen/config.h"

#include <string>

namespace kinglet::preen {

std::string apply_extensions(const std::string &formatted, const FmtConfig &config);

} // namespace kinglet::preen
