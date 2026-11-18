#pragma once
/* (HeyListen) Runtime option ids that are ours alone, in a header of their own: gpu/vk/vk_api.h is part of the shader stamp
   (nx2/build.py), and a change there makes every player prepare shaders again. The ids upstream uses (Hookshot's and
   Longshot's, in vk_api.h and the renderer's option table) are left alone: 10 is upstream's Text size, 60 Longshot's item
   wheel. The options file is keyed by row name, so the id is not stored anywhere. */

/* The Frame rate limit row (host/nn_vi.c asks, gpu/vk/vk_replay.cpp answers in frames a second: 60, 90, 120, 144, 165, 240) */
#define HL_OPT_FPS_LIMIT 90

/* The Skip title screen row (host/oot_autoload.c asks, gpu/vk/vk_replay.cpp answers 0 or 1). 44 is the id Longshot's change
   of the same row uses in vk_api.h (free in this tree: the ids there run 32 to 35, 40 to 43), so the two agree. */
#define HL_OPT_SKIP_TITLE 44

/* The Graphics tab's HUD layout row (longshot #1133; host/hos_wide_ui.c asks at each present, gpu/vk/vk_replay.cpp answers):
   on a screen that is not 16:9, 0 the HUD's corners at the screen's edges, 1 the HUD in the middle 16:9 as before
   host/hos_wide_ui.c (on a narrower screen fitted to its width, as the menus). HOS_WIDE_UI, when set, stands over it. 71 is
   Longshot's id for it; free here (90 is the Frame rate limit's). */
#define HL_OPT_HUD_LAYOUT 71
