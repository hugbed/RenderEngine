#include <Renderer/ImGuiVulkan.h>

#include <RHI/PhysicalDevice.h>

namespace
{
	static void CheckVkResult(VkResult result)
	{
		// empty for now
	}
}

ImGuiVulkan::ImGuiVulkan(const Resources& resources, vk::CommandBuffer commandBuffer)
	: m_device(resources.device)
	, m_renderPass(resources.renderPass)
{
	// Create a descriptor pool specifically for IMGUI
	VkDescriptorPoolSize poolSizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};
	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1000 * (uint32_t)IM_ARRAYSIZE(poolSizes);
	poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
	poolInfo.pPoolSizes = poolSizes;
	VkResult result = vkCreateDescriptorPool(g_device->Get(), &poolInfo, nullptr, &m_imguiDescriptorPool);
	if (result != VK_SUCCESS)
	{
		assert(false && "Could not initialize descriptor pool for imgui");
		return;
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForVulkan(resources.window, true);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = resources.instance;
	init_info.PhysicalDevice = resources.physicalDevice;
	init_info.Device = m_device;
	init_info.QueueFamily = resources.queueFamily;
	init_info.Queue = resources.queue;
	init_info.PipelineCache = VK_NULL_HANDLE;
	init_info.DescriptorPool = m_imguiDescriptorPool;
	init_info.Allocator = nullptr;
	init_info.MinImageCount = 2;
	init_info.ImageCount = resources.imageCount;
	init_info.CheckVkResultFn = &::CheckVkResult;
	init_info.PipelineInfoMain.RenderPass = m_renderPass;
	init_info.PipelineInfoMain.MSAASamples = (VkSampleCountFlagBits)resources.MSAASamples;
	if (!ImGui_ImplVulkan_Init(&init_info))
	{
		vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
		assert(false && "Could not initialize imgui");
		return;
	}

	// Create secondary command buffers
	{
		m_imguiCommandBuffers.clear();

		m_secondaryCommandPool = g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_physicalDevice->GetQueueFamilies().graphicsFamily.value()
		));

		m_imguiCommandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
			m_secondaryCommandPool.get(), vk::CommandBufferLevel::eSecondary, resources.imageCount
		));
	}
}

ImGuiVulkan::~ImGuiVulkan()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	m_imguiCommandBuffers.clear();
	m_secondaryCommandPool.reset();

	if (m_imguiDescriptorPool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
}

void ImGuiVulkan::Reset(const Resources& resources, vk::CommandBuffer commandBuffer)
{
	ImGui_ImplVulkan_Shutdown();

	m_device = resources.device;
	m_renderPass = resources.renderPass;

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = resources.instance;
	init_info.PhysicalDevice = resources.physicalDevice;
	init_info.Device = m_device;
	init_info.QueueFamily = resources.queueFamily;
	init_info.Queue = resources.queue;
	init_info.PipelineCache = VK_NULL_HANDLE;
	init_info.DescriptorPool = m_imguiDescriptorPool;
	init_info.Allocator = nullptr;
	init_info.MinImageCount = 2;
	init_info.ImageCount = resources.imageCount;
	init_info.PipelineInfoMain.RenderPass = m_renderPass;
	init_info.PipelineInfoMain.MSAASamples = (VkSampleCountFlagBits)resources.MSAASamples;
	init_info.CheckVkResultFn = &::CheckVkResult;
	assert(ImGui_ImplVulkan_Init(&init_info) && "Could not initialize imgui");
}

void ImGuiVulkan::BeginFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void ImGuiVulkan::RecordCommands(uint32_t frameIndex, VkFramebuffer framebuffer)
{
	auto& commandBuffer = m_imguiCommandBuffers[frameIndex];
	vk::CommandBufferInheritanceInfo info(
		m_renderPass, 0, framebuffer
	);
	commandBuffer->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
	{
		ImDrawData* drawData = ImGui::GetDrawData();
		if (drawData != nullptr)
			ImGui_ImplVulkan_RenderDrawData(drawData, *commandBuffer);
	}
	commandBuffer->end();
}

void ImGuiVulkan::EndFrame()
{
	ImGui::EndFrame();
	ImGui::Render();
}
