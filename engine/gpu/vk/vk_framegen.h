// Frame interpolation (vk_framegen.cpp): made frames shown between the game's (in a window; RX_VK_FRAMEGEN=0: off).
#pragma once

#include "vk_internal.h"

namespace mkwvk {

bool framegen_on();
/// Who makes the frames now (framegen_rows.h BY_OWN, BY_DLSS, BY_FSR3): the Frame generation by row's choice, except that DLSS
/// FG chosen is the renderer's own where DLSS-G does not run (not in this build, NGX's no) or has no temporal Upscaler's
/// inputs to make frames from (its multiplier is not capped by its runtime then)
int framegen_by_now();
/// (longshot ec76f696, extended) Frame generation's setting as it runs: the Insert menu's (opts.framegen: 0 Off, 1 Auto, 2 to 8 a
/// fixed multiplier) as the mode that makes the frames supports it (framegen_rows.h clamp): NVIDIA DLSS FG at most its runtime's
/// most made frames + 1 (vk_dlss.h dlss_fg_most: 6x on an RTX 50's runtime, 2x on an RTX 40's; that most is 0 otherwise: no cap)
/// and no Auto, AMD FSR 3.1 at 2x, the renderer's own up to 8x with Auto; a fixed multiplier then at most what leaves the game 20
/// frames a second under the Frame rate limit (framegen_rows.h limited). A saved value past it is kept in the options file
/// and is shown and run as the top
int framegen_now();
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
