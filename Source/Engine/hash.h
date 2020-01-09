#pragma once

// From Wikipedia: Fowler–Noll–Vo hash function
template <class T>
uint64_t fnv_hash(T* obj)
{
	const uint8_t* data = reinterpret_cast<const uint8_t*>(obj);

	constexpr uint64_t fnv_offset_basis = 0xcbf29ce484222325;
	constexpr uint64_t fnv_prime = 0x100000001b3;

	uint64_t hash = fnv_offset_basis;
	for (size_t i = 0ULL; i < sizeof(T); ++i)
	{
		hash = (hash ^ data[i]);
		hash = hash * fnv_prime;
	}
	return hash;
}
