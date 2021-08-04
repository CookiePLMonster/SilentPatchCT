#define NOMINMAX
#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"

void OnInitializeHook()
{
	auto Protect = ScopedUnprotect::UnprotectSectionOrFullModule( GetModuleHandle( nullptr ), ".text" );

	using namespace Memory;
	using namespace hook::txn;

	// Analog steering fix
	try
	{
		// Increase the threshold of analog-to-dpad mapping like on Dreamcast,
		// so analog input doesn't get interpreted as dpad as soon as it goes past the deadzone
		// Now it needs to be moved almost fully to the edge (add some tolerance to account for broken/weird gamepads)
		constexpr int32_t NEW_DEADZONE = std::numeric_limits<int16_t>::max() - 32;

		// Find the function processing the digital input
		auto func_start = reinterpret_cast<uintptr_t>(get_pattern("8B 40 04 2B C3"));
		auto func_end = reinterpret_cast<uintptr_t>(get_pattern("80 E3 01 5F 5E 8A C3"));

		auto left_analog_plus = make_range_pattern(func_start, func_end, "BA A9 1E 00 00").count(2);
		auto left_analog_minus = make_range_pattern(func_start, func_end, "BA 57 E1 FF FF").count(2);
		auto right_analog_plus = make_range_pattern(func_start, func_end, "BA F1 21 00 00").count(2);
		auto right_analog_minus = make_range_pattern(func_start, func_end, "B8 0F DE FF FF").count(2);

		left_analog_plus.for_each_result([=](hook::pattern_match match)
		{
			Patch<int32_t>(match.get<void>(1), NEW_DEADZONE);
		});

		left_analog_minus.for_each_result([=](hook::pattern_match match)
		{
			Patch<int32_t>(match.get<void>(1), -NEW_DEADZONE);
		});

		right_analog_plus.for_each_result([=](hook::pattern_match match)
		{
			Patch<int32_t>(match.get<void>(1), NEW_DEADZONE);
		});

		right_analog_minus.for_each_result([=](hook::pattern_match match)
		{
			Patch<int32_t>(match.get<void>(1), -NEW_DEADZONE);
		});
	}
	TXN_CATCH();
}