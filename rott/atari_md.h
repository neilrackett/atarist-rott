/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_md.h - MD/ROTT: ROTT's 3D view rendered by a SidecarTridge
 * Multi-device running the MD/ROTT firmware (sidecart/).
 *
 * The ST keeps running the game. When the firmware is found at startup it
 * renders the view instead: the ST sends it the level once (a snapshot),
 * then per frame the view, the objects that might be visible, the weapon
 * and whatever changed in the world, and copies the finished planar frame
 * out of the cartridge window. Without the firmware nothing here does
 * anything and the ST renders as before.
 *
 * Build switch ATARI_MD_RENDER; every hook below compiles away without it.
 */

#ifndef ATARI_MD_H
#define ATARI_MD_H

#ifndef ATARI_MD_RENDER
#define ATARI_MD_RENDER 0
#endif

#if defined(ATARI_NATIVE) && ATARI_MD_RENDER

/* Non-zero while the MD renders the view. */
extern int atari_md_active;
#define ATARI_MD_Active() (atari_md_active)

/* Startup, after the WADs are open: find the firmware and say hello. */
void ATARI_MD_Init(void);

/* rt_ted.c's precache list for the level being set up (PreCache). */
void ATARI_MD_BeginLumpList(void);
void ATARI_MD_AddLump(int lump);

/* The level was set up or a game loaded: send the snapshot before the
 * next frame. */
void ATARI_MD_LevelChanged(void);

/* An in-game tilemap write at (x, y); the new value is read when the next
 * frame goes out. */
void ATARI_MD_TileChanged(int x, int y);
/* An expression, so it can follow a tilemap write with the comma operator
 * even where the write is the whole body of an if. */
#define MD_TILE_TOUCH(x, y) \
  ((void)(atari_md_active ? (ATARI_MD_TileChanged((x), (y)), 0) : 0))

/* A masked wall changed (flags or textures): the next frame looks for
 * changes, which it otherwise does only every 8th frame. */
extern int atari_md_masked_dirty;
#define MD_MASKED_TOUCH() ((void)(atari_md_masked_dirty = 1))

/* Light sources around (x, y) changed (a lamp turned on or off). */
void ATARI_MD_LightsChanged(int x, int y);

/* I_SetPalette. */
void ATARI_MD_SetPalette(const unsigned char *palette);

/* WallRefresh worked out the view: the rest of this frame goes to the MD. */
void ATARI_MD_BeginFrame(int yzangle, int nonbobpheight, int weaponbobx,
                         int weaponboby);

/* ThreeDRefresh's overlays, recorded instead of drawn. mode: +1/-1 when
 * the weapon's scale is widened/narrowed by the bob, 0 otherwise. */
void ATARI_MD_Weapon(int xoff, int yoff, int shapenum, int mode);
void ATARI_MD_Eye(int x, int y, int shapenum);
void ATARI_MD_GasMask(int lump);
void ATARI_MD_Border(int color);
void ATARI_MD_Paused(void);
void ATARI_MD_Messages(const char *const *lines, int count);

/* I_FinishUpdate: if a frame was begun, send it and put the MD's view on
 * screen, converting only the HUD around it. Returns non-zero if it did,
 * in which case the caller skips its own C2P. */
int ATARI_MD_FinishUpdate(unsigned char *screen, const unsigned char *pixels);

/* The frame copy may still be running on the blitter: anything else that
 * draws to the screen calls this first. */
void ATARI_MD_BlitWait(void);

/* The MD's last frame, from the screen into the chunky screen, for the
 * effects that work on that (RotateBuffer). */
void ATARI_MD_ViewToChunky(unsigned char *chunky);

/* The automap is opening: fold what the MD has seen into mapseen[][]. */
void ATARI_MD_SyncMapSeen(void);

/* For the Help key's overlay. The first seven are over the last second or
 * so; the rest are counts since the level began. */
typedef struct {
  int fps10;     /* frames a second x10                               */
  int tics10;    /* game logic tics run each frame x10                 */
  int logic_ms;  /* running them each frame                            */
  int wait_ms;   /* the ST's wait for the MD each frame                */
  int cmd_ms;    /* sending the frame command each frame               */
  int render_ms; /* the MD's render of its frame                       */
  int c2p_ms;    /* the MD's dither + c2p of it                        */
  int sd_loads;  /* lumps the MD loaded from the SD card during play   */
  int evicts;    /* demand-loaded lumps it dropped for room            */
  int retries;   /* commands the ST sent again after a timeout         */
  int dups;      /* frames the MD rendered beyond those the ST sent    */
  int drops;     /* commands the MD dropped, its queue full            */
  int chkerrs;   /* commands that arrived with a bad checksum          */
} atari_md_stats_t;
void ATARI_MD_GetStats(atari_md_stats_t *s);

/* Once, after the Accelerator gave up: the status bars want drawing again
 * (the play loop does it, between frames). */
int ATARI_MD_TakeHudRedraw(void);

#if defined(ATARI_MD_AUTOTEST) && (ATARI_MD_AUTOTEST > 0)
/* Test runs: after frame `frame` is on screen, save it as SHOTnnn.PI1 if
 * it is one of the frames compared between MD and ST runs. */
void ATARI_MD_AutotestShot(int frame);
#endif

#else

#define ATARI_MD_Active() 0
#define MD_TILE_TOUCH(x, y) ((void)0)
#define MD_MASKED_TOUCH() ((void)0)

#endif

#endif /* ATARI_MD_H */
