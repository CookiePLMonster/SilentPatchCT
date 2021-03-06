#define NOMINMAX
#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"

namespace AnalogTriggersFix
{
	static float leftTrigger, rightTrigger;
	static void __stdcall SetTriggerValues(uintptr_t padData, uint32_t buttonsMask)
	{
		uint16_t* rightTriggerPtr = reinterpret_cast<uint16_t*>(padData + 0x18);
		uint16_t* leftTriggerPtr = reinterpret_cast<uint16_t*>(padData + 0x1A);

		const float leftClamped = std::min(1.0f, std::exchange(leftTrigger, 0.0f));
		const float rightClamped = std::min(1.0f, std::exchange(rightTrigger, 0.0f));

		auto leftVal = static_cast<uint16_t>(leftClamped * 255.0f);
		auto rightVal = static_cast<uint16_t>(rightClamped * 255.0f);
		
		if (leftVal != 0)
		{
			*leftTriggerPtr = leftVal;
		}
		else
		{
			*leftTriggerPtr = (buttonsMask & 0x20000) != 0 ? 255 : 0;
		}

		if (rightVal != 0)
		{
			*rightTriggerPtr = rightVal;
		}
		else
		{
			*rightTriggerPtr = (buttonsMask & 0x10000) != 0 ? 255 : 0;
		}
	}

	static __declspec(naked) void LoadDeadzone()
	{
		static constexpr float TRIGGER_FULL_PRESS_ZONE = 250.0f;
		_asm
		{
			fstp	[ebp+8]
			fld		[TRIGGER_FULL_PRESS_ZONE]
			retn
		}
	}
};

namespace AltF4Fix
{
	BOOL WINAPI PeekMessageA_HandleQuit(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
	{
		if (PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg) != FALSE)
		{
			return lpMsg->message != WM_QUIT;
		}
		return FALSE;
	}
	static auto* const pPeekMessageA_HandleQuit = &PeekMessageA_HandleQuit;
}

namespace WindowDimensionsFix
{
	HWND WINAPI CreateWindowExA_AdjustRect(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
	{
		dwStyle &= ~WS_MAXIMIZEBOX;
		if (X != CW_USEDEFAULT && nWidth != CW_USEDEFAULT)
		{
			RECT rect { X, Y, X + nWidth, Y + nHeight };
			if (AdjustWindowRectEx(&rect, dwStyle, hMenu != nullptr, dwExStyle) != FALSE)
			{
				X = rect.left;
				Y = rect.top;
				nWidth = rect.right - rect.left;
				nHeight = rect.bottom - rect.top;
			}
		}
		return CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
	}
	static auto* const pCreateWindowExA_AdjustRect = &CreateWindowExA_AdjustRect;
}

namespace DInputCrashFix
{
	static void* (*orgOperatorNew)(size_t size);
	static void* operatorNew_ZeroField(size_t size)
	{
		// Allocate and zero 4 bytes at +0x118
		void* result = orgOperatorNew(size);
		if (result != nullptr)
		{
			const uintptr_t addr = reinterpret_cast<uintptr_t>(result);
			void** ptr = reinterpret_cast<void**>(addr + 0x118);
			*ptr = nullptr;
		}
		return result;
	}
}

namespace ClassicHotkeysRestore
{
	static bool HotkeyModifierPressed()
	{
		// Shift + Alt
		return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 && (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
	}

	template<int vKey>
	static bool IsKeyJustPressed()
	{
		static bool pressed = false;
		if ((GetAsyncKeyState(vKey) & 0x8000) != 0)
		{
			if (!pressed)
			{
				pressed = true;
				return true;
			}
		}
		else
		{
			pressed = false;
		}
		return false;
	}

	static unsigned int* enableCameraCheats;
	static unsigned int* requestedCameraMode;

	static void (*orgProcessCameraChanges)();
	static void ProcessCameraChanges_PollHotkeys()
	{
		if (HotkeyModifierPressed())
		{
			if (IsKeyJustPressed<VK_F5>())
			{
				*enableCameraCheats = 1;
				*requestedCameraMode = 1;
			}
			else if (IsKeyJustPressed<VK_F6>())
			{
				*enableCameraCheats = 1;
				*requestedCameraMode = 2;
			}
			else if (IsKeyJustPressed<VK_F7>())
			{
				*enableCameraCheats = 1;
				*requestedCameraMode = 3;
			}
		}

		orgProcessCameraChanges();
	}

	static unsigned int* drawSpeedometer;

	static void (*orgReroutedFunc)(void* param);
	static void ReroutedFunc_ToggleSpeedometer(void* param)
	{
		if (HotkeyModifierPressed())
		{
			if (IsKeyJustPressed<VK_F8>())
			{
				*drawSpeedometer ^= 1;
			}
		}
		orgReroutedFunc(param);
	}
}

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

		// Now try to undo the old analog fix to avoid conflicts (let it throw an exception if the fix isn't here)
		auto old_fix = get_pattern("E9 07 A7 04 00 90 90");
		Patch(old_fix, {0x80, 0x3D, 0x5E, 0xFA, 0x64, 0x01, 0x00});
	}
	TXN_CATCH();


	// Analog triggers fix
	try
	{
		using namespace AnalogTriggersFix;

		auto read_trigger_data = pattern("FF D0 D8 1D ? ? ? ? DF E0 F6 C4 41 75 0D 81 4C 24 ? ? ? ? ? C6 44 24").count(2);
		auto set_trigger_data = reinterpret_cast<uintptr_t>(get_pattern("8B 4C 24 10 66 89 43 22", 8));
		auto set_trigger_data_jmp_dest = reinterpret_cast<uintptr_t>(get_pattern("BF ? ? ? ? F6 C1 20 75 04"));

		auto jmp = [](uintptr_t& addr, uintptr_t dest)
		{
			const ptrdiff_t offset = dest - (addr+2);
			if (offset >= INT8_MIN && offset <= INT8_MAX)
			{
				Patch(addr, { 0xEB, static_cast<uint8_t>(offset) });
				addr += 2;
			}
			else
			{
				InjectHook(addr, dest, PATCH_JUMP);
				addr += 5;
			}
		};

		auto fstp = [](uintptr_t& addr, float* var)
		{
			Patch(addr, { 0xD9, 0x1D });
			Patch(addr + 2, var);
			addr += 6;
		};

		auto fadd = [](uintptr_t& addr, float* var)
		{
			Patch(addr, { 0xD8, 0x05 });
			Patch(addr + 2, var);
			addr += 6;
		};

		{
			const auto match = read_trigger_data.get(0);
			const auto dest = reinterpret_cast<uintptr_t>(match.get<void>(0x1C));
			auto addr = reinterpret_cast<uintptr_t>(match.get<void>(2));

			fadd(addr, &rightTrigger);
			fstp(addr, &rightTrigger);
			jmp(addr, dest);
		}

		{
			const auto match = read_trigger_data.get(1);
			const auto dest = reinterpret_cast<uintptr_t>(match.get<void>(0x1C));
			auto addr = reinterpret_cast<uintptr_t>(match.get<void>(2));

			fadd(addr, &leftTrigger);
			fstp(addr, &leftTrigger);
			jmp(addr, dest);
		}

		{
			auto addr = set_trigger_data;

			Patch(addr, {0x51, 0x51, 0x53}); addr += 3; // push ecx / push ecx / push ebx
			InjectHook(addr, SetTriggerValues, PATCH_CALL); addr += 5;
			Patch(addr, { 0x59 }); addr += 1;  // pop ecx
			jmp(addr, set_trigger_data_jmp_dest);
		}

		// Now try to undo the old analog fix to avoid conflicts (let it throw an exception if the fix isn't here)
		auto old_fix1 = get_pattern("E9 C5 D5 0A 00");
		auto old_fix2 = get_pattern("E9 EA A6 04 00", 1);

		Patch(old_fix1, {0x0F, 0xB6, 0x44, 0xFB, 0x17});
		Patch(old_fix2, {0x47, 0x01, 0x00, 0x00});
	}
	TXN_CATCH();


	// Fixup XInput triggers digital press deadzone
	// Fixes a brief "digital press" of a trigger if the input value is below the analog input deadzone
	try
	{
		using namespace AnalogTriggersFix;

		auto load_deadzone = get_pattern("D9 5D 08 D9 EE");
		InjectHook(load_deadzone, LoadDeadzone, PATCH_CALL);
	}
	TXN_CATCH();


	// Fix Alt+F4 (short-circuit WM_QUIT)
	try
	{
		using namespace AltF4Fix;

		auto peek_msg = get_pattern("8B 1D ? ? ? ? 57 57 57 57", 2);
		Patch(peek_msg, &pPeekMessageA_HandleQuit);
	}
	TXN_CATCH();


	// Adjust the window rect to keep proper dimensions of the client area
	// Also disable the Maximize button and disable WM_GETMINMAXINFO handling so window is not forcibly constrained
	try
	{
		using namespace WindowDimensionsFix;

		auto create_window = get_pattern("FF 15 ? ? ? ? 89 86 ? ? ? ? 33 FF", 2);
		auto wnd_proc_minmax_jumptable = get_pattern("8D 46 FF 83 F8 23", 5);
		auto set_window_long_overlapped = get_pattern("68 ? ? ? ? 6A F0 51", 1);

		Patch(create_window, &pCreateWindowExA_AdjustRect);
		Patch<uint8_t>(wnd_proc_minmax_jumptable, 0x22);
		Patch<uint32_t>(set_window_long_overlapped, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX);
	}
	TXN_CATCH();


	// Fix DInput device crashes
	try
	{
		using namespace DInputCrashFix;

		auto operator_new = get_pattern("8B F0 83 C4 04 85 F6 74 71", -5);
		auto wrong_ptr_access = get_pattern("39 9E ? ? ? ? 74 23", 2);

		ReadCall(operator_new, orgOperatorNew);
		InjectHook(operator_new, operatorNew_ZeroField);

		// Should read field_118 instead of field_114 (copypaste error?)
		Patch<uint32_t>(wrong_ptr_access, 0x118);
	}
	TXN_CATCH();


	// Fix analog deadzones
	// Makes the analog deadzone smaller (0.24 -> 0.1) and digital deadzone bigger (0.95)
	// Fixes DInput analog steering and makes XInput steering smoother
	try
	{
		static constexpr float ANALOG_DEADZONE = 0.1f;
		static constexpr float DIGITAL_DEADZONE = 0.95f;

		float* deadzone_val = *get_pattern<float*>("51 8D 4C 24 10 D9 1C 24 E8 ? ? ? ? D9 44 24 10", -6 + 2);
		auto digital_deadzones = pattern("D9 5D 08 D9 05 ? ? ? ? D9 1C 24").count(4);

		*deadzone_val = ANALOG_DEADZONE;

		digital_deadzones.for_each_result([](hook::pattern_match match) {
			Patch(match.get<void>(3 + 2), &DIGITAL_DEADZONE);
		});
	}
	TXN_CATCH();


	// Restored old PC hotkeys:
	// Camera change (Shift + Alt + F5-F7)
	// Speedometer (Shift + Alt + F8)
	{
		using namespace ClassicHotkeysRestore;

		// Camera
		try
		{
			void* process_changes[] = {
				get_pattern("E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? BE ? ? ? ? 39 35"),
				get_pattern("E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 83 E8 00"),
			};

			auto enable_cam_cheats = *get_pattern<unsigned int*>("BE 01 00 00 00 39 3D", 5 + 2);
			auto requested_cam_mode = *get_pattern<unsigned int*>("74 4B A1 ? ? ? ? 3B C6", 2 + 1);

			enableCameraCheats = enable_cam_cheats;
			requestedCameraMode = requested_cam_mode;

			ReadCall(process_changes[0], orgProcessCameraChanges);
			for (void* addr : process_changes)
			{
				InjectHook(addr, ProcessCameraChanges_PollHotkeys);
			}
		}
		TXN_CATCH();

		// Speedometer
		try
		{
			auto draw_speedo_pattern = pattern("68 ? ? ? ? E8 ? ? ? ? 83 C4 04 BE ? ? ? ? E8 ? ? ? ? 83 3D ? ? ? ? 00").get_one();

			drawSpeedometer = *draw_speedo_pattern.get<unsigned int*>(25);

			ReadCall(draw_speedo_pattern.get<void>(5), orgReroutedFunc);
			InjectHook(draw_speedo_pattern.get<void>(5), ReroutedFunc_ToggleSpeedometer);
		}
		TXN_CATCH();
	}
}