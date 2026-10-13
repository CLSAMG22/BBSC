// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include "common/types.h"

namespace ShadNet {

struct CreateAccountResult {
    bool ok = false;
    std::string message; // for the user: "Account created" or why not
};

// Registers an account on a shadNet server (the same request as shadnet-sample's "register").
// Blocking - run it off the UI thread. secret_key is the server's RegistrationSecretKey, empty
// when the server has none. The server keeps the account; nothing is stored here.
CreateAccountResult CreateAccount(const std::string& host, u16 port, const std::string& npid,
                                  const std::string& password, const std::string& email,
                                  const std::string& secret_key, u32 timeout_ms = 8000);

// "host:port" (port defaults to 31313). False when it cannot be read.
bool SplitServerAddress(const std::string& address, std::string& host, u16& port);

} // namespace ShadNet
