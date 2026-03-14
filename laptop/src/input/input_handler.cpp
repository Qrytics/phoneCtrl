#include "input_handler.h"

#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

// ─── platform guard ──────────────────────────────────────────────────────────
#ifndef _WIN32
#  error "InputHandler requires Windows (SendInput / virtual-key codes)"
#endif

#include <windows.h>

// ─── Minimal JSON value parser (no external dependency) ──────────────────────
// We only need to extract string and integer fields from flat JSON objects.
namespace {

std::string extractString(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return {};

    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return {};

    // Skip whitespace
    while (++pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {}

    if (pos >= json.size()) return {};

    if (json[pos] == '"') {
        // String value
        auto start = pos + 1;
        auto end   = json.find('"', start);
        if (end == std::string::npos) return {};
        return json.substr(start, end - start);
    }
    return {};
}

int extractInt(const std::string& json, const std::string& key, int defaultVal = 0)
{
    std::string searchKey = "\"" + key + "\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return defaultVal;

    // Skip whitespace
    while (++pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {}

    if (pos >= json.size()) return defaultVal;

    // Read optional minus and digits
    if (json[pos] == '"') return defaultVal; // it's a string field

    std::string numStr;
    if (json[pos] == '-') numStr += '-', ++pos;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        numStr += json[pos++];
    }
    if (numStr.empty() || numStr == "-") return defaultVal;
    return std::stoi(numStr);
}

// Decode a hex string like "0x41" or "41" to an integer
int parseHexOrDec(const std::string& s)
{
    if (s.empty()) return 0;
    try {
        return std::stoi(s, nullptr, 0); // auto-detect 0x prefix
    } catch (...) {
        return 0;
    }
}

} // anonymous namespace

namespace phonectrl {
namespace input {

// ─── Coordinate scaling ───────────────────────────────────────────────────────
// SendInput with MOUSEEVENTF_ABSOLUTE uses a 65535-wide virtual coordinate space.
static constexpr int kAbsoluteMax = 65535;

InputHandler::InputHandler()  = default;
InputHandler::~InputHandler() = default;

void InputHandler::setScreenDimensions(int width, int height)
{
    screenWidth_  = width;
    screenHeight_ = height;
}

// ─── Mouse helpers ────────────────────────────────────────────────────────────

void InputHandler::mouseMove(int x, int y)
{
    // Clamp to screen bounds
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= screenWidth_)  x = screenWidth_  - 1;
    if (y >= screenHeight_) y = screenHeight_ - 1;

    // Map to absolute [0, 65535] coordinate space
    const int absX = MulDiv(x, kAbsoluteMax, screenWidth_  - 1);
    const int absY = MulDiv(y, kAbsoluteMax, screenHeight_ - 1);

    INPUT input{};
    input.type           = INPUT_MOUSE;
    input.mi.dx          = static_cast<LONG>(absX);
    input.mi.dy          = static_cast<LONG>(absY);
    input.mi.dwFlags     = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE |
                           MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &input, sizeof(INPUT));
}

void InputHandler::mouseButton(const std::string& button, bool down)
{
    INPUT input{};
    input.type = INPUT_MOUSE;

    if (button == "left") {
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else if (button == "right") {
        input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else if (button == "middle") {
        input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    } else {
        return; // unknown button
    }
    SendInput(1, &input, sizeof(INPUT));
}

void InputHandler::mouseScroll(int /*dx*/, int dy)
{
    INPUT input{};
    input.type       = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    // Positive dy = scroll up; WHEEL_DELTA = 120 per "notch"
    input.mi.mouseData = static_cast<DWORD>(dy * WHEEL_DELTA);
    SendInput(1, &input, sizeof(INPUT));
}

// ─── Keyboard helper ──────────────────────────────────────────────────────────

void InputHandler::keyEvent(int vk, bool down)
{
    if (vk <= 0 || vk > 0xFF) return;

    INPUT input{};
    input.type       = INPUT_KEYBOARD;
    input.ki.wVk     = static_cast<WORD>(vk);
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

// ─── Main dispatch ────────────────────────────────────────────────────────────

bool InputHandler::handleJson(const std::string& json)
{
    const std::string type = extractString(json, "type");
    if (type.empty()) {
        std::cerr << "[InputHandler] Missing 'type' field in: " << json << "\n";
        return false;
    }

    if (type == "mousemove") {
        int x = extractInt(json, "x");
        int y = extractInt(json, "y");
        mouseMove(x, y);

    } else if (type == "mousedown") {
        std::string btn = extractString(json, "button");
        if (btn.empty()) btn = "left";
        mouseButton(btn, true);

    } else if (type == "mouseup") {
        std::string btn = extractString(json, "button");
        if (btn.empty()) btn = "left";
        mouseButton(btn, false);

    } else if (type == "click") {
        int x = extractInt(json, "x");
        int y = extractInt(json, "y");
        std::string btn = extractString(json, "button");
        if (btn.empty()) btn = "left";
        mouseMove(x, y);
        mouseButton(btn, true);
        mouseButton(btn, false);

    } else if (type == "scroll") {
        int dx = extractInt(json, "dx");
        int dy = extractInt(json, "dy");
        mouseScroll(dx, dy);

    } else if (type == "keydown") {
        std::string key = extractString(json, "key");
        keyEvent(parseHexOrDec(key), true);

    } else if (type == "keyup") {
        std::string key = extractString(json, "key");
        keyEvent(parseHexOrDec(key), false);

    } else if (type == "keypress") {
        std::string key = extractString(json, "key");
        int vk = parseHexOrDec(key);
        keyEvent(vk, true);
        keyEvent(vk, false);

    } else {
        std::cerr << "[InputHandler] Unknown event type: " << type << "\n";
        return false;
    }

    return true;
}

} // namespace input
} // namespace phonectrl
