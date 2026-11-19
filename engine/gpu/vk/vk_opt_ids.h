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

/* The HDR tone mapper (after Longshot ddb156df, #2481): HDR set to Native with its Tone mapper row on PsychoV35 (vk_menu.cpp
   scene_tonemap_on; gpu/vk/vk_replay.cpp answers it). host/nn_vi.c makes the scene buffer RGBA16F only then, so Native HDR with the
   row on Vanilla keeps the game's R11G11B10F and the temporal upscalers, which take only that scene. 47, Longshot's id for it in
   its vk_api.h, so the two agree. */
#define HL_OPT_HDR_TONEMAP 47
