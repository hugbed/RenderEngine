#pragma once

#include <cassert>

#define ASSERT(expr) { assert(expr); } // todo: put this somewhere else

#define IMPLEMENT_MOVABLE_ONLY(CLASS_NAME_HERE) \
	CLASS_NAME_HERE(const CLASS_NAME_HERE&) = delete; \
	CLASS_NAME_HERE& operator=(const CLASS_NAME_HERE&) = delete; \
	CLASS_NAME_HERE(CLASS_NAME_HERE&&) noexcept = default; \
	CLASS_NAME_HERE& operator=(CLASS_NAME_HERE&&) noexcept = default;

class DeferredDestructible
{
public:
	virtual ~DeferredDestructible() {}
};
