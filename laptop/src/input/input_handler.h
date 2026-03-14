#pragma once

#include <string>

namespace phonectrl {
namespace input {

/**
 * InputHandler
 *
 * Parses JSON input-event messages sent by the phone and injects them into
 * the Windows input system using the SendInput() API.
 *
 * Supported event types (JSON field "type"):
 *
 *   "mousemove"   – { "type": "mousemove", "x": <int>, "y": <int> }
 *                   Moves the cursor to absolute screen coordinates (x, y).
 *
 *   "mousedown"   – { "type": "mousedown", "button": "left"|"right"|"middle" }
 *                   Presses the specified mouse button.
 *
 *   "mouseup"     – { "type": "mouseup",   "button": "left"|"right"|"middle" }
 *                   Releases the specified mouse button.
 *
 *   "click"       – { "type": "click", "x": <int>, "y": <int>,
 *                       "button": "left"|"right" }
 *                   Moves then clicks the specified button.
 *
 *   "scroll"      – { "type": "scroll", "dx": <int>, "dy": <int> }
 *                   Sends a wheel event. Positive dy = scroll up.
 *
 *   "keydown"     – { "type": "keydown", "key": <VK hex string> }
 *                   Presses a virtual key (e.g. "0x41" for 'A').
 *
 *   "keyup"       – { "type": "keyup",   "key": <VK hex string> }
 *                   Releases a virtual key.
 *
 *   "keypress"    – { "type": "keypress", "key": <VK hex string> }
 *                   Press + release a virtual key.
 *
 * Usage:
 *   InputHandler ih;
 *   ih.setScreenDimensions(1920, 1080);  // laptop screen resolution
 *   ih.handleJson(message);              // called from network callback
 */
class InputHandler {
public:
    InputHandler();
    ~InputHandler();

    /**
     * Tell the handler the resolution of the captured screen so it can
     * correctly scale phone touch coordinates to absolute screen coordinates.
     */
    void setScreenDimensions(int width, int height);

    /**
     * Parse and execute a JSON input-event string.
     * @returns true if the event was recognised and injected successfully.
     */
    bool handleJson(const std::string& json);

private:
    void mouseMove(int x, int y);
    void mouseButton(const std::string& button, bool down);
    void mouseScroll(int dx, int dy);
    void keyEvent(int vk, bool down);

    int screenWidth_  = 1920;
    int screenHeight_ = 1080;
};

} // namespace input
} // namespace phonectrl
