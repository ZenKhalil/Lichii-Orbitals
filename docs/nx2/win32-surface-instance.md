# Win32 Vulkan surface instance

The Win32 surface creation path passed a null `hinstance` with its existing
window handle. Vulkan validation reported
`VUID-VkWin32SurfaceCreateInfoKHR-hinstance-01307` during startup. Vulkan requires
the instance handle associated with the surface's window.

The Windows path now obtains that handle from the actual HWND with
`GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)`. A null result fails initialization
before invoking Vulkan. The window, surface flags, extension lookup, allocator,
result handling and other platform paths are unchanged.

Primary references:

- [Microsoft GetWindowLongPtrW documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowlongptrw)
  defines `GWLP_HINSTANCE` as the window's application instance handle.
- [VkWin32SurfaceCreateInfoKHR](https://docs.vulkan.org/refpages/latest/refpages/source/VkWin32SurfaceCreateInfoKHR.html)
  requires a valid `hinstance` for the associated window.
- The [pinned upstream implementation](https://github.com/zortos293/Lichii/blob/cbb512ee7a9de9d9dbe2f865ec2e468888503214/src/video_core/vulkan_common/vulkan_surface.cpp)
  still supplies null. This is a local correction, not an upstream patch port.

The production translation unit compiled with the existing Windows clang-cl
Release/ThinLTO flags. The later Candidate 3 application run reported no Win32
instance violation; see its [runtime record](vulkan-validation-findings.md#build-3-candidate-3).
There is no mirrored logic test for this API wiring change. Removing this startup violation
does not establish a fix for the reported split-screen artifact, numeric texel
mismatches or a separate private-data feature report. No private-data feature is
enabled by this change.
