#pragma once

#include <cstdint>

// todo (hbedard): the implementation below can be mistaken for the one above with the default param!

static uint64_t fnv_hash_data(const uint8_t* data, size_t size, uint64_t seed = 0xcbf29ce484222325)
{
	const uint64_t fnv_offset_basis = seed;
	constexpr uint64_t fnv_prime = 0x100000001b3;

	uint64_t hash = fnv_offset_basis;
	for (size_t i = 0ULL; i < size; ++i)
	{
		hash = (hash ^ data[i]);
		hash = hash * fnv_prime;
	}
	return hash;
}

// From Wikipedia: Fowler–Noll–Vo hash function
template <class T>
uint64_t fnv_hash(T&& obj, uint64_t seed = 0xcbf29ce484222325)
{
	return fnv_hash_data(reinterpret_cast<const uint8_t*>(&obj), sizeof(T), seed);
}
