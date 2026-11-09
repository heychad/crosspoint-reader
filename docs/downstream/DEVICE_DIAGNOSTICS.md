# Device diagnostics

On the X4 Pro, **Settings → System → Device diagnostics** shows free bytes, the largest free block, and the sum of each memory region's lowest free-byte mark since boot for internal RAM and PSRAM. Select **Refresh measurements** for a new sample. Region lows can occur at different times, so their sum is not necessarily a minimum total that the device actually reached.

The first sample is taken on entry; refreshed samples are taken while the screen is open. They include the diagnostics activity and do not measure page-turn speed, reader memory, or battery usage. Region low-water marks reset on reboot. The screen reads HAL counters; it does not write a log or change device settings. Physical display fit and readings on the X4 Pro need verification.
