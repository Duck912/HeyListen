// Frame interpolation (vk_framegen.cpp): made frames shown between the game's (in a window; RX_VK_FRAMEGEN=0: off).
#pragma once

#include "vk_internal.h"

namespace mkwvk {

bool framegen_on();
/// (the performance statistics, perf_report.cpp) what makes the frames now, as perf_report::FrameGen: 0 none (the row Off, or the
/// interpolation not running: RX_VK_FRAMEGEN=0, no window, its images not made), 1 the renderer's own, 2 AMD FSR 3.1's frame
/// interpolation (a fixed 2x where it runs; a present it takes no inputs for is made by the renderer's own). *mult: the frames shown
/// for each of the game's, 1 with none made; *automatic: the row is Auto and *mult the most a game frame is given (the display's
/// refresh over 30, 2 to 4: how many are made depends on the game's rate)
int framegen_engine(int* mult, bool* automatic);
/// Records the frame's processing into the present's recording (src: the window texture's view, sw x sh, the crop
/// x, y, w, h); false when interpolation cannot run (the present goes to the window as without it)
bool framegen_record(VkCommandBuffer cmd, VkImageView src, uint32_t sw, uint32_t sh, const int crop[4]);
/// The first frame the last framegen_record made (null when it made none), w x h, GENERAL
VkImage framegen_made_image(uint32_t* w, uint32_t* h);
/// After that recording's submit (value): the display thread shows its frames
void framegen_queue(uint64_t value);
/// The window's present of an image (GENERAL, w x h; crop as above or null), vk_replay.cpp: the display thread's
void present_image(VkImage src, uint32_t w, uint32_t h, const int crop[4], VkImageView view = VK_NULL_HANDLE);
/// The Insert menu's upscaling of a frame to the window (dw x dh), recorded: the image to show, or null when it is off
VkImage framegen_scale(VkCommandBuffer cmd, VkImageView src, VkImage image, uint32_t sw, uint32_t sh, const int crop[4], uint32_t dw, uint32_t dh);
VkImageView framegen_scaled_view();   // (hookshot) the view of framegen_scale's last image
/// (hookshot) The HDR10 picture of src's crop at dw x dh (the Insert menu's HDR), GENERAL; null: none (the SDR picture then)
VkImage framegen_hdr(VkCommandBuffer cmd, VkImageView src, uint32_t sw, uint32_t sh, const int crop[4], uint32_t dw, uint32_t dh);

} // namespace mkwvk
