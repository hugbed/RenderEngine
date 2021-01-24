#pragma once

#include <map>

#include "glm_includes.h"

using KeyID = int;

enum class KeyAction
{
	ePressed = 1,
	eRepeated = 2
};

struct Inputs
{
	std::map<KeyID, KeyAction> keyState;

	glm::vec2 lastCursorPos = glm::vec2(0.0f);
	glm::vec2 cursorPos = glm::vec2(0.0f);
	glm::vec2 scrollOffset = glm::vec2(0.0f);

	bool scrollOffsetReceived = false;
	bool isMouseDown = false;
	bool mouseWasCaptured = false;
};

class InputSystem
{
public:
	// For this frame
	void CaptureMouseInputs(bool captureMouseInputs) { m_inputs.mouseWasCaptured = captureMouseInputs; }

	const Inputs& GetFrameInputs() const { return m_inputs; }

	// Reset state for next frame
	void EndFrame();

	static void OnMouseButton(void* data, int button, int action, int mods);
	static void OnMouseScroll(void* data, double xOffset, double yOffset);
	static void OnCursorPosition(void* data, double xPos, double yPos);
	static void OnKey(void* data, int key, int scancode, int action, int mods);

private:
	Inputs m_inputs;
};
