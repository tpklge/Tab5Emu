#pragma once
bool paludo_launcher_available();
void paludo_request_return();
bool paludo_return_requested();
// Requires the caller to persist state and release exclusive filesystem use.
bool paludo_return_to_launcher();
