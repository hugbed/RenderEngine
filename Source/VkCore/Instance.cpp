#include "Instance.h"

#include "Window.h"

static char const* AppName = "RenderEngineTest";
static char const* EngineName = "RenderEngine";

Instance::Instance(const Window& window)
{
	vk::ApplicationInfo appInfo(AppName, 1, EngineName, 1, VK_API_VERSION_1_2);

	auto extensions = GetRequiredExtensions(window);
	auto layers = debug_utils::kValidationLayers;
	vk::InstanceCreateInfo instanceInfo({}, &appInfo, layers.size(), layers.data(), extensions.size(), extensions.data());

	m_instance = vk::createInstanceUnique(instanceInfo);

#ifdef DEBUG_UTILS_ENABLED
	m_debugUtilsMessenger = debug_utils::SetupDebugMessenger(m_instance.get());
#endif
}

std::vector<const char*> Instance::GetRequiredExtensions(const Window& window)
{
	auto extensions = window.GetRequiredExtensions();

#ifdef DEBUG_UTILS_ENABLED
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

	return extensions;
}
