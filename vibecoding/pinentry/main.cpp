// main.cpp — barebones pinentry demo
//
// Usage: ./pinentry-helper [pinentry-binary]
//   ./pinentry-helper                 -> system default (pinentry-gtk/-qt/-curses/...)
//   ./pinentry-helper ./pinentry-raylib -> the raylib+raygui pinentry in this repo
#include "pinentry_client.hpp"

#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    std::string_view backend = (argc > 1) ? argv[1] : "pinentry";
    auto client = PinentryClient::create(backend);
    if (!client) {
        std::cerr << "error: " << client.error() << "\n";
        return 1;
    }

    client->setTitle("methuselah");
    client->setDesc("Enter your passphrase to unlock the credential store");
    client->setPrompt("Passphrase:");
    client->setOkLabel("Unlock");
    client->setCancelLabel("Cancel");

    auto pin = client->getPin();
    if (!pin) {
        std::cerr << "cancelled or error: " << pin.error() << "\n";
        return 1;
    }

    // Use the passphrase here. Never log it.
    std::cout << "got passphrase, length=" << pin->size() << "\n";

    // Wipe it from memory once done. explicit_bzero() (glibc) resists
    // being optimized away, unlike a plain memset.
    explicit_bzero(pin->data(), pin->size());

    return 0;
}
