#pragma once

#include <cstdint>
#include <limits>

enum class MaterialShadingDomain : uint32_t
{
	Surface = 0,
	Count
};

enum class MaterialShadingModel : uint32_t
{
	Unlit = 0,
	Lit = 1,
	Count
};

static constexpr MaterialShadingModel kShadingModel[] = {
	MaterialShadingModel::Unlit,
	MaterialShadingModel::Lit
};

class MaterialHandle
{
public:
	constexpr MaterialHandle()
		: m_raw((std::numeric_limits<uint32_t>::max)())
	{
	}

	MaterialHandle(MaterialShadingDomain domain, MaterialShadingModel model, uint32_t index)
		: m_shadingDomain(static_cast<uint32_t>(domain))
		, m_shadingModel(static_cast<uint32_t>(model))
		, m_id(index)
	{
		assert(index < (1 << 30));
	}

	static constexpr MaterialHandle Invalid()
	{
		return MaterialHandle();
	}

	MaterialShadingDomain GetDomain() const
	{
		return static_cast<MaterialShadingDomain>(m_shadingDomain);
	}

	MaterialShadingModel GetModel() const
	{
		return static_cast<MaterialShadingModel>(m_shadingModel);
	}

	void SetID(uint32_t id)
	{
		m_id = id;
	}

	uint32_t GetID() const
	{
		return m_id;
	}

	void IncrementID()
	{
		++m_id;
	}

	auto operator<=>(const MaterialHandle& other) const
	{
		return m_raw <=> other.m_raw;
	}

	bool operator==(const MaterialHandle& other) const
	{
		return m_raw == other.m_raw;
	}

	bool operator!=(const MaterialHandle& other) const
	{
		return m_raw == other.m_raw;
	}

private:
	union {
		struct
		{
			uint32_t m_shadingDomain : 2;
			uint32_t m_shadingModel : 2;
			uint32_t m_id : 30;
		};
		uint32_t m_raw;
	};
};