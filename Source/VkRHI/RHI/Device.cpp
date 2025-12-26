#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>
#include <RHI/debug_utils.h>
#include <defines.h>

#include <set>

Device* g_device;

void Device::Init(const Instance& instance, const PhysicalDevice& physicalDevice)
{
	if (g_device == nullptr)
		g_device = new Device(instance, physicalDevice);
}

void Device::Term()
{
	if (g_device != nullptr)
		delete g_device;
}

Device::Device(const Instance& instance, const PhysicalDevice& physicalDevice)
{
	float queuePriority = 1.0f;

	const auto& indices = physicalDevice.GetQueueFamilies();

	std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };
	
	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		queueCreateInfos.emplace_back(
			vk::DeviceQueueCreateFlags(),	// flags
			queueFamily,					// queueFamilyIndex
			1,								// queueCount
			&queuePriority					// pQueuePriorities
		);
	}

	vk::PhysicalDeviceFeatures2 deviceFeatures;
	vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures;
	deviceFeatures.pNext = descriptorIndexingFeatures;
	vkGetPhysicalDeviceFeatures2(physicalDevice.Get(), deviceFeatures);
	deviceFeatures.features.samplerAnisotropy = true;

	// Non-uniform indexing and update after bind
	// binding flags for textures, uniforms, and buffers
	// are required for our extension
	assert(descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing);
	assert(descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind);
	assert(descriptorIndexingFeatures.shaderUniformBufferArrayNonUniformIndexing);
	assert(descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind);
	assert(descriptorIndexingFeatures.shaderStorageBufferArrayNonUniformIndexing);
	assert(descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind);

	vk::DeviceCreateInfo createInfo(
		vk::DeviceCreateFlags{},						// flags
		static_cast<uint32_t>(queueCreateInfos.size()),	// queueCreateInfoCount
		queueCreateInfos.data()							// pQueueCreateInfos
	);
	createInfo.pNext = &deviceFeatures;

	auto deviceExtensions = physicalDevice.GetDeviceExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

#ifdef DEBUG_UTILS_ENABLED
	createInfo.enabledLayerCount = static_cast<uint32_t>(debug_utils::kValidationLayers.size());
	createInfo.ppEnabledLayerNames = debug_utils::kValidationLayers.data();
#else
	createInfo.enabledLayerCount = 0;
#endif

	m_device = physicalDevice.Get().createDeviceUnique(createInfo);

	// Create memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.instance = static_cast<VkInstance>(instance.Get());
	allocatorInfo.physicalDevice = static_cast<VkPhysicalDevice>(physicalDevice.Get());
	allocatorInfo.device = static_cast<VkDevice>(m_device.get());
	vmaCreateAllocator(&allocatorInfo, &m_allocator);
}

Device::~Device()
{
	vmaDestroyAllocator(m_allocator);
}

// Could be in header
vk::Queue Device::GetQueue(uint32_t index) const
{
	return m_device->getQueue(index, 0);
}

vk::Queue Device::GetGraphicsQueue() const
{
	auto queueFamilies = g_physicalDevice->GetQueueFamilies();
	return GetQueue(queueFamilies.graphicsFamily.value());
}

vk::Queue Device::GetPresentQueue() const
{
	auto queueFamilies = g_physicalDevice->GetQueueFamilies();
	return GetQueue(queueFamilies.presentFamily.value());
}
