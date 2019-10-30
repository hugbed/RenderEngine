#include "Instance.h"

#include "Window.h"

#include "DebugUtils.h"

Instance::Instance(const Window& window)
{
	vk::ApplicationInfo appInfo("RenderEngine");

	auto extensions = GetRequiredExtensions(window);

	vk::InstanceCreateInfo instanceInfo;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledExtensionCount = extensions.size();
	instanceInfo.ppEnabledExtensionNames = extensions.data();
	instanceInfo.enabledLayerCount = 0;

	m_instance = vk::createInstanceUnique(instanceInfo);

	DebugUtils::SetupDebugMessenger(m_instance.get());
}

std::vector<const char*> Instance::GetRequiredExtensions(const Window& window)
{
	auto extensions = window.GetRequiredExtensions();

	if (DebugUtils::kIsEnabled)
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	return extensions;
}
