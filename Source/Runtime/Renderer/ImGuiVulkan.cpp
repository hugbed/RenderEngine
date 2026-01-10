#include <Renderer/ImGuiVulkan.h>

#include <RHI/PhysicalDevice.h>
#include <RHI/Swapchain.h>

namespace ImGuiVulkan_Private
{
	static void CheckVkResult(VkResult result)
	{
		// empty for now
	}
}

ImGuiVulkan::ImGuiVulkan(const Resources& resources)
	: m_device(resources.device)
{
	Init(resources);
}

ImGuiVulkan::~ImGuiVulkan()
{
	Shutdown();
}

void ImGuiVulkan::Init(const Resources& resources)
{
	using namespace ImGuiVulkan_Private;

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
	init_info.CheckVkResultFn = &CheckVkResult;
	init_info.PipelineInfoMain.MSAASamples = (VkSampleCountFlagBits)resources.MSAASamples;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = resources.pipelineRenderingCreateInfo;
	init_info.UseDynamicRendering = true;
	if (!ImGui_ImplVulkan_Init(&init_info))
	{
		vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
		assert(false && "Could not initialize imgui");
		return;
	}
}

void ImGuiVulkan::Shutdown()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	if (m_imguiDescriptorPool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
}

void ImGuiVulkan::Reset(const Resources& resources)
{
	m_device = resources.device;

	Shutdown();
	Init(resources);
}

void ImGuiVulkan::BeginFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void ImGuiVulkan::Render(vk::CommandBuffer commandBuffer, uint32_t imageIndex, const Swapchain& swapchain)
{
	RenderingInfo renderingInfo = swapchain.GetRenderingInfo(imageIndex, {}, {});
	commandBuffer.beginRendering(&renderingInfo.info);
	{
		if (ImDrawData* drawData = ImGui::GetDrawData())
		{
			ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
		}
	}
	commandBuffer.endRendering();
}

void ImGuiVulkan::EndFrame()
{
	ImGui::EndFrame();
	ImGui::Render();
}
