#pragma once

#include <memory>
#include <string>

#include "engine/tls.hpp"

namespace gio::detail {

std::unique_ptr<Connection> network_tls_connect(const TlsConfig& config,
                                                const Endpoint& e,
                                                double timeout_seconds,
                                                Status& s,
                                                std::string& tls_error);

}
