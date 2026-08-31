# PaperMono OTP Refresh Policy

All subsequent PaperMono display work in this project must follow the official `M5PaperMono-OTP-Demo` state model. The current Arduino/M5GFX implementation is considered transitional until its display transport is replaced with the OTP driver path.

## Current implementation status (2026-09-01)

This document specifies the **target**, not a claim that the current sketch already implements it.

- The sketch uses 2-bit M5Canvas buffers but draws activity tiles with black/white Bayer dots: thresholds 3, 6, 9 and 12 out of 16. It does not intentionally request native four-gray updates.
- Canvas sizes are 800×480 (calendar), 192×118 (time at 584,114) and 220×14 (Hub status at 460,19), allocated with PSRAM preferred.
- `epd_text` pushes increment an application counter; after 10, the next scheduled update is `epd_quality`. Boot, date changes and manual serial time-setting also request quality refreshes.
- The sketch does not explicitly verify BUSY completion, baseline validity, Deep Sleep Mode 1, or alignment for all rectangles; it delegates display transport to M5GFX. The status rectangle is not byte-aligned in application code. Successful-return counting is not hardware completion verification.
- One initial `SCANNING` status push and a subsequent final-state/data push may occur after boot. Changed final BLE states trigger a calendar push; unchanged failures do not. This is not yet a single fully coalesced OTP transaction.
- Do not infer complete OTP compliance or a power-saving guarantee from the current 10-update counter. Further driver migration and hardware acceptance remain work items.

## Non-negotiable rules

1. Compose the complete next monochrome frame in PSRAM before touching the display controller.
2. Establish a valid monochrome baseline with the official full-refresh sequence before the first visible partial update.
3. Use the SSD1677 built-in OTP waveforms; do not upload a custom LUT unless its DC balance has been independently validated.
4. Never overlap refreshes. Wait for BUSY to complete before accepting another display transaction.
5. Enter Deep Sleep Mode 1 after refresh so controller RAM is preserved. Resume with the same hardware-reset sequence used by the official demo; do not insert a software reset that destroys the partial-refresh state.
6. Count only successfully completed partial refreshes. After 10 partial refreshes, the next scheduled display update is a monochrome full refresh and resets the counter.
7. Any four-gray/full-gray operation invalidates the monochrome baseline. Rebuild it with a monochrome full refresh before another partial refresh. The calendar should remain 1-bit and avoid this path.
8. Coalesce RTC, BLE, battery, and activity changes into one pending frame. Data arrival alone must not cause multiple pushes.
9. The changing rectangle must be aligned to SSD1677 byte packing: X origin rounds down to a multiple of 8 and the right edge rounds up to a multiple of 8.
10. Log the refresh type, partial count, aligned rectangle, BUSY duration, and reason once per completed refresh.

## Calendar scheduling contract

- Boot or lost baseline: full monochrome OTP refresh.
- Minute change with unchanged date/activity: one aligned partial refresh of the time region.
- Date change, activity change, or layout change: render one complete frame and perform one refresh chosen by state; do not issue a preliminary clear.
- Ten successful partials reached: promote the next pending update to full monochrome OTP refresh.
- BLE bursts: decode and merge in memory, then submit at most one display update.

Official references:

- https://docs.m5stack.com/en/core/PaperMono
- https://github.com/m5stack/M5PaperMono-OTP-Demo
- https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/main/components/EDP_OTP_LUT_demo/src/EDP_OTP_LUT_demo.cpp
