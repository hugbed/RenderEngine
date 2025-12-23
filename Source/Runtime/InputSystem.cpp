#include <InputSystem.h>

#include <GLFW/glfw3.h>
#include <glm_includes.h>

void InputSystem::EndFrame()
{
	m_inputs.scrollOffsetReceived = false;
	m_inputs.lastCursorPos = m_inputs.cursorPos;
	m_inputs.keyState.clear();
}

// static
void InputSystem::OnMouseButton(void* data, int button, int action, int mods)
{
	InputSystem* inputSystem = reinterpret_cast<InputSystem*>(data);

	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
		inputSystem->m_inputs.isLeftMouseDown = true;
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
		inputSystem->m_inputs.isLeftMouseDown = false;
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
		inputSystem->m_inputs.isRightMouseDown = true;
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
		inputSystem->m_inputs.isRightMouseDown = false;
}

// static
void InputSystem::OnMouseScroll(void* data, double xOffset, double yOffset)
{
	InputSystem* inputSystem = reinterpret_cast<InputSystem*>(data);
	inputSystem->m_inputs.scrollOffset = glm::vec2(xOffset, yOffset);
	inputSystem->m_inputs.scrollOffsetReceived = true;
}

// static
void InputSystem::OnCursorPosition(void* data, double xPos, double yPos)
{
	InputSystem* inputSystem = reinterpret_cast<InputSystem*>(data);
	inputSystem->m_inputs.cursorPos = glm::vec2(xPos, yPos);
}

// static
void InputSystem::OnKey(void* data, int key, int scancode, int action, int mods)
{
	InputSystem* inputSystem = reinterpret_cast<InputSystem*>(data);
	if (action == GLFW_REPEAT)
		inputSystem->m_inputs.keyState[key] = KeyAction::eRepeated;
	else if (action == GLFW_PRESS)
		inputSystem->m_inputs.keyState[key] = KeyAction::ePressed;
	else if (action == GLFW_RELEASE)
		inputSystem->m_inputs.keyState[key] = KeyAction::eReleased;
}
