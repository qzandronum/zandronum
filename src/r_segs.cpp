// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//		All the clipping: columns, horizontal spans, sky columns.
//
// This file contains some code from the Build Engine.
//
// "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
// Ken Silverman's official web site: "http://www.advsys.net/ken"
// See the included license file "BUILDLIC.TXT" for license info.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <stddef.h>

#include "templates.h"
#include "i_system.h"

#include "doomdef.h"
#include "doomstat.h"
#include "doomdata.h"
#include "p_lnspec.h"

#include "r_local.h"
#include "r_sky.h"
#include "v_video.h"

#include "m_swap.h"
#include "w_wad.h"
#include "stats.h"
#include "a_sharedglobal.h"
#include "d_net.h"
#include "g_level.h"
#include "r_bsp.h"
#include "r_plane.h"
#include "r_segs.h"
#include "r_3dfloors.h"
#include "r_draw.h"
#include "v_palette.h"
#include "r_data/colormaps.h"

#define WALLYREPEAT 8


CVAR(Bool, r_np2, true, 0)
EXTERN_CVAR(Bool, r_fullbrightignoresectorcolor);

//CVAR (Int, ty, 8, 0)
//CVAR (Int, tx, 8, 0)

#define HEIGHTBITS 12
#define HEIGHTSHIFT (FRACBITS-HEIGHTBITS)

extern double globaluclip, globaldclip;

PortalDrawseg* CurrentPortal = NULL;
int CurrentPortalUniq = 0;
bool CurrentPortalInSkybox = false;

// OPTIMIZE: closed two sided lines as single sided

// killough 1/6/98: replaced globals with statics where appropriate

static bool		segtextured;	// True if any of the segs textures might be visible.
bool		markfloor;		// False if the back side is the same plane.
bool		markceiling;
FTexture *toptexture;
FTexture *bottomtexture;
FTexture *midtexture;
fixed_t rw_offset_top;
fixed_t rw_offset_mid;
fixed_t rw_offset_bottom;


int		wallshade;

short	walltop[MAXWIDTH];	// [RH] record max extents of wall
short	wallbottom[MAXWIDTH];
short	wallupper[MAXWIDTH];
short	walllower[MAXWIDTH];
float	swall[MAXWIDTH];
fixed_t	lwall[MAXWIDTH];
double	lwallscale;

//
// regular wall
//
extern double	rw_backcz1, rw_backcz2;
extern double	rw_backfz1, rw_backfz2;
extern double	rw_frontcz1, rw_frontcz2;
extern double	rw_frontfz1, rw_frontfz2;

int				rw_ceilstat, rw_floorstat;
bool			rw_mustmarkfloor, rw_mustmarkceiling;
bool			rw_prepped;
bool			rw_markportal;
bool			rw_havehigh;
bool			rw_havelow;

float			rw_light;		// [RH] Scale lights with viewsize adjustments
float			rw_lightstep;
float			rw_lightleft;

static double	rw_frontlowertop;

static int		rw_x;
static int		rw_stopx;
fixed_t			rw_offset;
static double	rw_scalestep;
static double	rw_midtexturemid;
static double	rw_toptexturemid;
static double	rw_bottomtexturemid;
static double	rw_midtexturescalex;
static double	rw_midtexturescaley;
static double	rw_toptexturescalex;
static double	rw_toptexturescaley;
static double	rw_bottomtexturescalex;
static double	rw_bottomtexturescaley;

FTexture		*rw_pic;

static fixed_t	*maskedtexturecol;

static void R_RenderDecal (side_t *wall, DBaseDecal *first, drawseg_t *clipper, int pass);
static void WallSpriteColumn (void (*drawfunc)(const BYTE *column, const FTexture::Span *spans));
void wallscan_np2(int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat, double top, double bot, bool mask);
static void wallscan_np2_ds(drawseg_t *ds, int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat);
static void call_wallscan(int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat, bool mask);

//=============================================================================
//
// CVAR r_fogboundary
//
// If true, makes fog look more "real" by shading the walls separating two
// sectors with different fog.
//=============================================================================

CVAR(Bool, r_fogboundary, true, 0)

inline bool IsFogBoundary (sector_t *front, sector_t *back)
{
	return r_fogboundary && fixedcolormap == NULL && front->ColorMap->Fade &&
		front->ColorMap->Fade != back->ColorMap->Fade &&
		(front->GetTexture(sector_t::ceiling) != skyflatnum || back->GetTexture(sector_t::ceiling) != skyflatnum);
}

//=============================================================================
//
// CVAR r_drawmirrors
//
// Set to false to disable rendering of mirrors
//=============================================================================

CVAR(Bool, r_drawmirrors, true, 0)

//
// R_RenderMaskedSegRange
//
float *MaskedSWall;
float MaskedScaleY;

static void BlastMaskedColumn (FTexture *tex, bool useRt)
{
	// calculate lighting
	if (fixedcolormap == NULL && fixedlightlev < 0)
	{
		R_SetColorMapLight(basecolormap, rw_light, wallshade);
	}

	dc_iscale = xs_Fix<16>::ToFix(MaskedSWall[dc_x] * MaskedScaleY);
 	if (sprflipvert)
		sprtopscreen = CenterY + dc_texturemid * spryscale;
	else
		sprtopscreen = CenterY - dc_texturemid * spryscale;
	
	// killough 1/25/98: here's where Medusa came in, because
	// it implicitly assumed that the column was all one patch.
	// Originally, Doom did not construct complete columns for
	// multipatched textures, so there were no header or trailer
	// bytes in the column referred to below, which explains
	// the Medusa effect. The fix is to construct true columns
	// when forming multipatched textures (see r_data.c).

	// draw the texture
	const FTexture::Span *spans;
	const BYTE *pixels;
	if (r_swtruecolor && !drawer_needs_pal_input)
		pixels = (const BYTE *)tex->GetColumnBgra(maskedtexturecol[dc_x] >> FRACBITS, &spans);
	else
		pixels = tex->GetColumn(maskedtexturecol[dc_x] >> FRACBITS, &spans);
	R_DrawMaskedColumn(pixels, spans, useRt);
	rw_light += rw_lightstep;
	spryscale += rw_scalestep;
}

// Clip a midtexture to the floor and ceiling of the sector in front of it.
void ClipMidtex(int x1, int x2)
{
	short most[MAXWIDTH];

	WallMost(most, curline->frontsector->ceilingplane, &WallC);
	for (int i = x1; i < x2; ++i)
	{
		if (wallupper[i] < most[i])
			wallupper[i] = most[i];
	}
	WallMost(most, curline->frontsector->floorplane, &WallC);
	for (int i = x1; i < x2; ++i)
	{
		if (walllower[i] > most[i])
			walllower[i] = most[i];
	}
}

void R_RenderFakeWallRange(drawseg_t *ds, int x1, int x2);

void R_RenderMaskedSegRange (drawseg_t *ds, int x1, int x2)
{
	FTexture	*tex;
	int			i;
	sector_t	tempsec;		// killough 4/13/98
	double		texheight, texheightscale;
	bool		notrelevant = false;
	double		rowoffset;
	bool		wrap = false;

	const sector_t *sec;

	sprflipvert = false;

	curline = ds->curline;

	// killough 4/11/98: draw translucent 2s normal textures
	// [RH] modified because we don't use user-definable translucency maps
	ESPSResult drawmode;

	drawmode = R_SetPatchStyle (LegacyRenderStyles[curline->linedef->flags & ML_ADDTRANS ? STYLE_Add : STYLE_Translucent],
		(float)MIN(curline->linedef->alpha, 1.),	0, 0);

	if ((drawmode == DontDraw && !ds->bFogBoundary && !ds->bFakeBoundary))
	{
		return;
	}

	NetUpdate ();

	frontsector = curline->frontsector;
	backsector = curline->backsector;

	tex = TexMan(curline->sidedef->GetTexture(side_t::mid), true);
	if (i_compatflags & COMPATF_MASKEDMIDTEX)
	{
		tex = tex->GetRawTexture();
	}

	// killough 4/13/98: get correct lightlevel for 2s normal textures
	sec = R_FakeFlat (frontsector, &tempsec, NULL, NULL, false);

	basecolormap = sec->ColorMap;	// [RH] Set basecolormap

	wallshade = ds->shade;
	rw_lightstep = ds->lightstep;
	rw_light = ds->light + (x1 - ds->x1) * rw_lightstep;

	if (fixedlightlev < 0)
	{
		if (!(fake3D & FAKE3D_CLIPTOP))
		{
			sclipTop = sec->ceilingplane.ZatPoint(ViewPos);
		}
		for (i = frontsector->e->XFloor.lightlist.Size() - 1; i >= 0; i--)
		{
			if (sclipTop <= frontsector->e->XFloor.lightlist[i].plane.Zat0())
			{
				lightlist_t *lit = &frontsector->e->XFloor.lightlist[i];
				basecolormap = lit->extra_colormap;
				wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
				break;
			}
		}
	}

	mfloorclip = openings + ds->sprbottomclip - ds->x1;
	mceilingclip = openings + ds->sprtopclip - ds->x1;

	// [RH] Draw fog partition
	if (ds->bFogBoundary)
	{
		R_DrawFogBoundary (x1, x2, mceilingclip, mfloorclip);
		if (ds->maskedtexturecol == -1)
		{
			goto clearfog;
		}
	}
	if ((ds->bFakeBoundary && !(ds->bFakeBoundary & 4)) || drawmode == DontDraw)
	{
		goto clearfog;
	}

	MaskedSWall = (float *)(openings + ds->swall) - ds->x1;
	MaskedScaleY = ds->yscale;
	maskedtexturecol = (fixed_t *)(openings + ds->maskedtexturecol) - ds->x1;
	spryscale = ds->iscale + ds->iscalestep * (x1 - ds->x1);
	rw_scalestep = ds->iscalestep;

	if (fixedlightlev >= 0)
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : basecolormap, 0, FIXEDLIGHT2SHADE(fixedlightlev));
	else if (fixedcolormap != NULL)
		R_SetColorMapLight(fixedcolormap, 0, 0);

	// find positioning
	texheight = tex->GetScaledHeightDouble();
	texheightscale = fabs(curline->sidedef->GetTextureYScale(side_t::mid));
	if (texheightscale != 1)
	{
		texheight = texheight / texheightscale;
	}
	if (curline->linedef->flags & ML_DONTPEGBOTTOM)
	{
		dc_texturemid = MAX(frontsector->GetPlaneTexZ(sector_t::floor), backsector->GetPlaneTexZ(sector_t::floor)) + texheight;
	}
	else
	{
		dc_texturemid = MIN(frontsector->GetPlaneTexZ(sector_t::ceiling), backsector->GetPlaneTexZ(sector_t::ceiling));
	}

	rowoffset = curline->sidedef->GetTextureYOffset(side_t::mid);

	wrap = (curline->linedef->flags & ML_WRAP_MIDTEX) || (curline->sidedef->Flags & WALLF_WRAP_MIDTEX);
	if (!wrap)
	{ // Texture does not wrap vertically.
		double textop;

		if (MaskedScaleY < 0)
		{
			MaskedScaleY = -MaskedScaleY;
			sprflipvert = true;
		}
		if (tex->bWorldPanning)
		{
			// rowoffset is added before the multiply so that the masked texture will
			// still be positioned in world units rather than texels.
			dc_texturemid += rowoffset - ViewPos.Z;
			textop = dc_texturemid;
			dc_texturemid *= MaskedScaleY;
		}
		else
		{
			// rowoffset is added outside the multiply so that it positions the texture
			// by texels instead of world units.
			textop = dc_texturemid + rowoffset / MaskedScaleY - ViewPos.Z;
			dc_texturemid = (dc_texturemid - ViewPos.Z) * MaskedScaleY + rowoffset;
		}
		if (sprflipvert)
		{
			MaskedScaleY = -MaskedScaleY;
			dc_texturemid -= tex->GetHeight() << FRACBITS;
		}

		// [RH] Don't bother drawing segs that are completely offscreen
		if (globaldclip * ds->sz1 < -textop && globaldclip * ds->sz2 < -textop)
		{ // Texture top is below the bottom of the screen
			goto clearfog;
		}

		if (globaluclip * ds->sz1 > texheight - textop && globaluclip * ds->sz2 > texheight - textop)
		{ // Texture bottom is above the top of the screen
			goto clearfog;
		}

		if ((fake3D & FAKE3D_CLIPBOTTOM) && textop < sclipBottom - ViewPos.Z)
		{
			notrelevant = true;
			goto clearfog;
		}
		if ((fake3D & FAKE3D_CLIPTOP) && textop - texheight > sclipTop - ViewPos.Z)
		{
			notrelevant = true;
			goto clearfog;
		}

		WallC.sz1 = ds->sz1;
		WallC.sz2 = ds->sz2;
		WallC.sx1 = ds->sx1;
		WallC.sx2 = ds->sx2;

		if (fake3D & FAKE3D_CLIPTOP)
		{
			OWallMost(wallupper, textop < sclipTop - ViewPos.Z ? textop : sclipTop - ViewPos.Z, &WallC);
		}
		else
		{
			OWallMost(wallupper, textop, &WallC);
		}
		if (fake3D & FAKE3D_CLIPBOTTOM)
		{
			OWallMost(walllower, textop - texheight > sclipBottom - ViewPos.Z ? textop - texheight : sclipBottom - ViewPos.Z, &WallC);
		}
		else
		{
			OWallMost(walllower, textop - texheight, &WallC);
		}

		for (i = x1; i < x2; i++)
		{
			if (wallupper[i] < mceilingclip[i])
				wallupper[i] = mceilingclip[i];
		}
		for (i = x1; i < x2; i++)
		{
			if (walllower[i] > mfloorclip[i])
				walllower[i] = mfloorclip[i];
		}

		if (CurrentSkybox)
		{ // Midtex clipping doesn't work properly with skyboxes, since you're normally below the floor
		  // or above the ceiling, so the appropriate end won't be clipped automatically when adding
		  // this drawseg.
			if ((curline->linedef->flags & ML_CLIP_MIDTEX) ||
				(curline->sidedef->Flags & WALLF_CLIP_MIDTEX))
			{
				ClipMidtex(x1, x2);
			}
		}

		mfloorclip = walllower;
		mceilingclip = wallupper;

		// draw the columns one at a time
		if (drawmode == DoDraw0)
		{
			for (dc_x = x1; dc_x < x2; ++dc_x)
			{
				BlastMaskedColumn (tex, false);
			}
		}
		else
		{
			// [RH] Draw up to four columns at once
			int stop = x2 & ~3;

			if (x1 >= x2)
				goto clearfog;

			dc_x = x1;

			while ((dc_x < stop) && (dc_x & 3))
			{
				BlastMaskedColumn (tex, false);
				dc_x++;
			}

			while (dc_x < stop)
			{
				rt_initcols(nullptr);
				BlastMaskedColumn (tex, true); dc_x++;
				BlastMaskedColumn (tex, true); dc_x++;
				BlastMaskedColumn (tex, true); dc_x++;
				BlastMaskedColumn (tex, true);
				rt_draw4cols (dc_x - 3);
				dc_x++;
			}

			while (dc_x < x2)
			{
				BlastMaskedColumn (tex, false);
				dc_x++;
			}
		}
	}
	else
	{ // Texture does wrap vertically.
		if (tex->bWorldPanning)
		{
			// rowoffset is added before the multiply so that the masked texture will
			// still be positioned in world units rather than texels.
			dc_texturemid = (dc_texturemid - ViewPos.Z + rowoffset) * MaskedScaleY;
		}
		else
		{
			// rowoffset is added outside the multiply so that it positions the texture
			// by texels instead of world units.
			dc_texturemid = (dc_texturemid - ViewPos.Z) * MaskedScaleY + rowoffset;
		}

		WallC.sz1 = ds->sz1;
		WallC.sz2 = ds->sz2;
		WallC.sx1 = ds->sx1;
		WallC.sx2 = ds->sx2;

		if (CurrentSkybox)
		{ // Midtex clipping doesn't work properly with skyboxes, since you're normally below the floor
		  // or above the ceiling, so the appropriate end won't be clipped automatically when adding
		  // this drawseg.
			if ((curline->linedef->flags & ML_CLIP_MIDTEX) ||
				(curline->sidedef->Flags & WALLF_CLIP_MIDTEX))
			{
				ClipMidtex(x1, x2);
			}
		}

		if (fake3D & FAKE3D_CLIPTOP)
		{
			OWallMost(wallupper, sclipTop - ViewPos.Z, &WallC);
			for (i = x1; i < x2; i++)
			{
				if (wallupper[i] < mceilingclip[i])
					wallupper[i] = mceilingclip[i];
			}
			mceilingclip = wallupper;
		}
		if (fake3D & FAKE3D_CLIPBOTTOM)
		{
			OWallMost(walllower, sclipBottom - ViewPos.Z, &WallC);
			for (i = x1; i < x2; i++)
			{
				if (walllower[i] > mfloorclip[i])
					walllower[i] = mfloorclip[i];
			}
			mfloorclip = walllower;
		}

		rw_offset = 0;
		rw_pic = tex;
		wallscan_np2_ds(ds, x1, x2, mceilingclip, mfloorclip, MaskedSWall, maskedtexturecol, ds->yscale);
	}

clearfog:
	R_FinishSetPatchStyle ();
	if (ds->bFakeBoundary & 3)
	{
		R_RenderFakeWallRange(ds, x1, x2);
	}
	if (!notrelevant)
	{
		if (fake3D & FAKE3D_REFRESHCLIP)
		{
			if (!wrap)
			{
				assert(ds->bkup >= 0);
				memcpy(openings + ds->sprtopclip, openings + ds->bkup, (ds->x2 - ds->x1) * 2);
			}
		}
		else
		{
			clearbufshort(openings + ds->sprtopclip - ds->x1 + x1, x2 - x1, viewheight);
		}
	}
	return;
}

// kg3D - render one fake wall
void R_RenderFakeWall(drawseg_t *ds, int x1, int x2, F3DFloor *rover)
{
	int i;
	double xscale;
	double yscale;

	fixed_t Alpha = Scale(rover->alpha, OPAQUE, 255);
	ESPSResult drawmode;
	drawmode = R_SetPatchStyle (LegacyRenderStyles[rover->flags & FF_ADDITIVETRANS ? STYLE_Add : STYLE_Translucent],
		Alpha, 0, 0);

	if(drawmode == DontDraw) {
		R_FinishSetPatchStyle();
		return;
	}

	rw_lightstep = ds->lightstep;
	rw_light = ds->light + (x1 - ds->x1) * rw_lightstep;

	mfloorclip = openings + ds->sprbottomclip - ds->x1;
	mceilingclip = openings + ds->sprtopclip - ds->x1;

	spryscale = ds->iscale + ds->iscalestep * (x1 - ds->x1);
	rw_scalestep = ds->iscalestep;
	MaskedSWall = (float *)(openings + ds->swall) - ds->x1;

	// find positioning
	side_t *scaledside;
	side_t::ETexpart scaledpart;
	if (rover->flags & FF_UPPERTEXTURE)
	{
		scaledside = curline->sidedef;
		scaledpart = side_t::top;
	}
	else if (rover->flags & FF_LOWERTEXTURE)
	{
		scaledside = curline->sidedef;
		scaledpart = side_t::bottom;
	}
	else
	{
		scaledside = rover->master->sidedef[0];
		scaledpart = side_t::mid;
	}
	xscale = rw_pic->Scale.X * scaledside->GetTextureXScale(scaledpart);
	yscale = rw_pic->Scale.Y * scaledside->GetTextureYScale(scaledpart);

	double rowoffset = curline->sidedef->GetTextureYOffset(side_t::mid) + rover->master->sidedef[0]->GetTextureYOffset(side_t::mid);
	double planez = rover->model->GetPlaneTexZ(sector_t::ceiling);
	rw_offset = FLOAT2FIXED(curline->sidedef->GetTextureXOffset(side_t::mid) + rover->master->sidedef[0]->GetTextureXOffset(side_t::mid));
	if (rowoffset < 0)
	{
		rowoffset += rw_pic->GetHeight();
	}
	dc_texturemid = (planez - ViewPos.Z) * yscale;
	if (rw_pic->bWorldPanning)
	{
		// rowoffset is added before the multiply so that the masked texture will
		// still be positioned in world units rather than texels.

		dc_texturemid = dc_texturemid + rowoffset * yscale;
		rw_offset = xs_RoundToInt(rw_offset * xscale);
	}
	else
	{
		// rowoffset is added outside the multiply so that it positions the texture
		// by texels instead of world units.
		dc_texturemid += rowoffset;
	}

	if (fixedlightlev >= 0)
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : basecolormap, 0, FIXEDLIGHT2SHADE(fixedlightlev));
	else if (fixedcolormap != NULL)
		R_SetColorMapLight(fixedcolormap, 0, 0);

	WallC.sz1 = ds->sz1;
	WallC.sz2 = ds->sz2;
	WallC.sx1 = ds->sx1;
	WallC.sx2 = ds->sx2;
	WallC.tleft.X = ds->cx;
	WallC.tleft.Y = ds->cy;
	WallC.tright.X = ds->cx + ds->cdx;
	WallC.tright.Y = ds->cy + ds->cdy;
	WallT = ds->tmapvals;

	OWallMost(wallupper, sclipTop - ViewPos.Z, &WallC);
	OWallMost(walllower, sclipBottom - ViewPos.Z, &WallC);

	for (i = x1; i < x2; i++)
	{
		if (wallupper[i] < mceilingclip[i])
			wallupper[i] = mceilingclip[i];
	}
	for (i = x1; i < x2; i++)
	{
		if (walllower[i] > mfloorclip[i])
			walllower[i] = mfloorclip[i];
	}

	PrepLWall (lwall, curline->sidedef->TexelLength*xscale, ds->sx1, ds->sx2);
	wallscan_np2_ds(ds, x1, x2, wallupper, walllower, MaskedSWall, lwall, yscale);
	R_FinishSetPatchStyle();
}

// kg3D - walls of fake floors
void R_RenderFakeWallRange (drawseg_t *ds, int x1, int x2)
{
	FTexture *const DONT_DRAW = ((FTexture*)(intptr_t)-1);
	int i,j;
	F3DFloor *rover, *fover = NULL;
	int passed, last;
	double floorHeight;
	double ceilingHeight;

	sprflipvert = false;
	curline = ds->curline;

	frontsector = curline->frontsector;
	backsector = curline->backsector;

	if (backsector == NULL)
	{
		return;
	}
	if ((ds->bFakeBoundary & 3) == 2)
	{
		sector_t *sec = backsector;
		backsector = frontsector;
		frontsector = sec;
	}

	floorHeight = backsector->CenterFloor();
	ceilingHeight = backsector->CenterCeiling();

	// maybe fix clipheights
	if (!(fake3D & FAKE3D_CLIPBOTTOM)) sclipBottom = floorHeight;
	if (!(fake3D & FAKE3D_CLIPTOP))    sclipTop = ceilingHeight;

	// maybe not visible
	if (sclipBottom >= frontsector->CenterCeiling()) return;
	if (sclipTop <= frontsector->CenterFloor()) return;

	if (fake3D & FAKE3D_DOWN2UP)
	{ // bottom to viewz
		last = 0;
		for (i = backsector->e->XFloor.ffloors.Size() - 1; i >= 0; i--) 
		{
			rover = backsector->e->XFloor.ffloors[i];
			if (!(rover->flags & FF_EXISTS)) continue;

			// visible?
			passed = 0;
			if (!(rover->flags & FF_RENDERSIDES) || rover->top.plane->isSlope() || rover->bottom.plane->isSlope() ||
				rover->top.plane->Zat0() <= sclipBottom ||
				rover->bottom.plane->Zat0() >= ceilingHeight ||
				rover->top.plane->Zat0() <= floorHeight)
			{
				if (!i)
				{
					passed = 1;
				}
				else
				{
					continue;
				}
			}

			rw_pic = NULL;
			if (rover->bottom.plane->Zat0() >= sclipTop || passed) 
			{
				if (last)
				{
					break;
				}
				// maybe wall from inside rendering?
				fover = NULL;
				for (j = frontsector->e->XFloor.ffloors.Size() - 1; j >= 0; j--)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model)
					{ // never
						fover = NULL;
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, it's bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->top.plane->Zat0() <= sclipBottom) continue; // no
					if (fover->bottom.plane->Zat0() >= sclipTop)
					{ // no, last possible
 						fover = NULL;
						break;
					}
					// it is, render inside?
					if (!(fover->flags & (FF_BOTHPLANES|FF_INVERTPLANES)))
					{ // no
						fover = NULL;
					}
					break;
				}
				// nothing
				if (!fover || j == -1)
				{
					break;
				}
				// correct texture
				if (fover->flags & rover->flags & FF_SWIMMABLE)
				{	// don't ever draw (but treat as something has been found)
					rw_pic = DONT_DRAW;
				}
				else if(fover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if(fover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(fover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			} 
			else if (frontsector->e->XFloor.ffloors.Size()) 
			{
				// maybe not visible?
				fover = NULL;
				for (j = frontsector->e->XFloor.ffloors.Size() - 1; j >= 0; j--)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model) // never
					{
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, it's bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->top.plane->Zat0() <= sclipBottom) continue; // no
					if (fover->bottom.plane->Zat0() >= sclipTop)
					{ // visible, last possible
 						fover = NULL;
						break;
					}
					if ((fover->flags & FF_SOLID) == (rover->flags & FF_SOLID) &&
						!(!(fover->flags & FF_SOLID) && (fover->alpha == 255 || rover->alpha == 255))
					)
					{
						break;
					}
					if (fover->flags & rover->flags & FF_SWIMMABLE)
					{ // don't ever draw (but treat as something has been found)
						rw_pic = DONT_DRAW;
					}
					fover = NULL; // visible
					break;
				}
				if (fover && j != -1)
				{
					fover = NULL;
					last = 1;
					continue; // not visible
				}
			}
			if (!rw_pic) 
			{
				fover = NULL;
				if (rover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if(rover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(rover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			}
			// correct colors now
			basecolormap = frontsector->ColorMap;
			wallshade = ds->shade;
			if (fixedlightlev < 0)
			{
				if ((ds->bFakeBoundary & 3) == 2)
				{
					for (j = backsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if (sclipTop <= backsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &backsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
				else
				{
					for (j = frontsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if (sclipTop <= frontsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &frontsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
			}
			if (rw_pic != DONT_DRAW)
			{
				R_RenderFakeWall(ds, x1, x2, fover ? fover : rover);
			}
			else rw_pic = NULL;
			break;
		}
	}
	else
	{ // top to viewz
		for (i = 0; i < (int)backsector->e->XFloor.ffloors.Size(); i++)
		{
			rover = backsector->e->XFloor.ffloors[i];
			if (!(rover->flags & FF_EXISTS)) continue;

			// visible?
			passed = 0;
			if (!(rover->flags & FF_RENDERSIDES) ||
				rover->top.plane->isSlope() || rover->bottom.plane->isSlope() ||
				rover->bottom.plane->Zat0() >= sclipTop ||
				rover->top.plane->Zat0() <= floorHeight ||
				rover->bottom.plane->Zat0() >= ceilingHeight)
			{
				if ((unsigned)i == backsector->e->XFloor.ffloors.Size() - 1)
				{
					passed = 1;
				}
				else
				{
					continue;
				}
			}
			rw_pic = NULL;
			if (rover->top.plane->Zat0() <= sclipBottom || passed)
			{ // maybe wall from inside rendering?
				fover = NULL;
				for (j = 0; j < (int)frontsector->e->XFloor.ffloors.Size(); j++)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model)
					{ // never
						fover = NULL;
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, it's bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->bottom.plane->Zat0() >= sclipTop) continue; // no
					if (fover->top.plane->Zat0() <= sclipBottom)
					{ // no, last possible
 						fover = NULL;
						break;
					}
					// it is, render inside?
					if (!(fover->flags & (FF_BOTHPLANES|FF_INVERTPLANES)))
					{ // no
						fover = NULL;
					}
					break;
				}
				// nothing
				if (!fover || (unsigned)j == frontsector->e->XFloor.ffloors.Size())
				{
					break;
				}
				// correct texture
				if (fover->flags & rover->flags & FF_SWIMMABLE)
				{
					rw_pic = DONT_DRAW;	// don't ever draw (but treat as something has been found)
				}
				else if (fover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if (fover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(fover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			}
			else if (frontsector->e->XFloor.ffloors.Size())
			{ // maybe not visible?
				fover = NULL;
				for (j = 0; j < (int)frontsector->e->XFloor.ffloors.Size(); j++)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model)
					{ // never
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, its bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->bottom.plane->Zat0() >= sclipTop) continue; // no
					if (fover->top.plane->Zat0() <= sclipBottom)
					{ // visible, last possible
 						fover = NULL;
						break;
					}
					if ((fover->flags & FF_SOLID) == (rover->flags & FF_SOLID) &&
						!(!(rover->flags & FF_SOLID) && (fover->alpha == 255 || rover->alpha == 255))
					)
					{
						break;
					}
					if (fover->flags & rover->flags & FF_SWIMMABLE)
					{ // don't ever draw (but treat as something has been found)
						rw_pic = DONT_DRAW;
					}
					fover = NULL; // visible
					break;
				}
				if (fover && (unsigned)j != frontsector->e->XFloor.ffloors.Size())
				{ // not visible
					break;
				}
			}
			if (rw_pic == NULL)
			{
				fover = NULL;
				if (rover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if (rover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(rover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			}
			// correct colors now
			basecolormap = frontsector->ColorMap;
			wallshade = ds->shade;
			if (fixedlightlev < 0)
			{
				if ((ds->bFakeBoundary & 3) == 2)
				{
					for (j = backsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if (sclipTop <= backsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &backsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
				else
				{
					for (j = frontsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if(sclipTop <= frontsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &frontsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
			}

			if (rw_pic != DONT_DRAW)
			{
				R_RenderFakeWall(ds, x1, x2, fover ? fover : rover);
			}
			else
			{
				rw_pic = NULL;
			}
			break;
		}
	}
	return;
}

EXTERN_CVAR(Bool, r_mipmap)

struct WallscanSampler
{
	WallscanSampler() { }
	WallscanSampler(int y1, float swal, double yrepeat, fixed_t xoffset, FTexture *texture, const BYTE*(*getcol)(FTexture *texture, int x));

	uint32_t uv_pos;
	uint32_t uv_step;
	uint32_t uv_max;

	const BYTE *source;
	const BYTE *source2;
	uint32_t texturefracx;
	uint32_t height;
};

WallscanSampler::WallscanSampler(int y1, float swal, double yrepeat, fixed_t xoffset, FTexture *texture, const BYTE*(*getcol)(FTexture *texture, int x))
{
	if (!r_swtruecolor)
	{
		height = texture->GetHeight();
		int uv_fracbits = 32 - texture->HeightBits;
		uv_max = height << uv_fracbits;

		// Find start uv in [0-base_height[ range.
		// Not using xs_ToFixed because it rounds the result and we need something that always rounds down to stay within the range.
		double uv_stepd = swal * yrepeat;
		double v = (dc_texturemid + uv_stepd * (y1 - CenterY + 0.5)) / height;
		v = v - floor(v);
		v *= height;
		v *= (1 << uv_fracbits);

		uv_pos = (uint32_t)v;
		uv_step = xs_ToFixed(uv_fracbits, uv_stepd);
		if (uv_step == 0) // To prevent divide by zero elsewhere
			uv_step = 1;

		source = getcol(texture, xoffset >> FRACBITS);
		source2 = nullptr;
		texturefracx = 0;
	}
	else
	{
		// Normalize to 0-1 range:
		double uv_stepd = swal * yrepeat;
		double v = (dc_texturemid + uv_stepd * (y1 - CenterY + 0.5)) / texture->GetHeight();
		v = v - floor(v);
		double v_step = uv_stepd / texture->GetHeight();

		if (isnan(v) || isnan(v_step)) // this should never happen, but it apparently does..
		{
			uv_stepd = 0.0;
			v = 0.0;
			v_step = 0.0;
		}

		// Convert to uint32:
		uv_pos = (uint32_t)(v * 0x100000000LL);
		uv_step = (uint32_t)(v_step * 0x100000000LL);
		uv_max = 0;

		// Texture mipmap and filter selection:
		if (getcol != R_GetColumn)
		{
			source = getcol(texture, xoffset >> FRACBITS);
			source2 = nullptr;
			height = texture->GetHeight();
			texturefracx = 0;
		}
		else
		{
			double magnitude = fabs(uv_stepd * 2);
			bool magnifying = magnitude < 1.0f;

			int mipmap_offset = 0;
			int mip_width = texture->GetWidth();
			int mip_height = texture->GetHeight();
			if (r_mipmap && texture->Mipmapped())
			{
				uint32_t xpos = (uint32_t)((((uint64_t)xoffset) << FRACBITS) / mip_width);
				double texture_bias = 1.7f;
				double level = MAX(magnitude - 3.0, 0.0);
				while (level > texture_bias)
				{
					mipmap_offset += mip_width * mip_height;
					level *= 0.5f;
					mip_width = MAX(mip_width >> 1, 1);
					mip_height = MAX(mip_height >> 1, 1);
				}
				xoffset = (xpos >> FRACBITS) * mip_width;
			}

			const uint32_t *pixels = texture->GetPixelsBgra() + mipmap_offset;

			bool filter_nearest = (magnifying && !r_magfilter) || (!magnifying && !r_minfilter);
			if (filter_nearest)
			{
				int tx = (xoffset >> FRACBITS) % mip_width;
				if (tx < 0)
					tx += mip_width;
				source = (BYTE*)(pixels + tx * mip_height);
				source2 = nullptr;
				height = mip_height;
				texturefracx = 0;
			}
			else
			{
				int tx0 = (xoffset >> FRACBITS) % mip_width;
				if (tx0 < 0)
					tx0 += mip_width;
				int tx1 = (tx0 + 1) % mip_width;
				source = (BYTE*)(pixels + tx0 * mip_height);
				source2 = (BYTE*)(pixels + tx1 * mip_height);
				height = mip_height;
				texturefracx = (xoffset >> (FRACBITS - 4)) & 15;
			}
		}
	}
}

// Draw a column with support for non-power-of-two ranges
void wallscan_drawcol1(int x, int y1, int y2, WallscanSampler &sampler, DWORD(*draw1column)())
{
	if (r_swtruecolor)
	{
		int count = y2 - y1;

		dc_source = sampler.source;
		dc_source2 = sampler.source2;
		dc_texturefracx = sampler.texturefracx;
		dc_dest = (ylookup[y1] + x) * 4 + dc_destorg;
		dc_count = count;
		dc_iscale = sampler.uv_step;
		dc_texturefrac = sampler.uv_pos;
		dc_textureheight = sampler.height;
		draw1column();

		uint64_t step64 = sampler.uv_step;
		uint64_t pos64 = sampler.uv_pos;
		sampler.uv_pos = (uint32_t)(pos64 + step64 * count);
	}
	else
	{
		if (sampler.uv_max == 0) // power of two
		{
			int count = y2 - y1;

			dc_source = sampler.source;
			dc_source2 = sampler.source2;
			dc_texturefracx = sampler.texturefracx;
			dc_dest = (ylookup[y1] + x) + dc_destorg;
			dc_count = count;
			dc_iscale = sampler.uv_step;
			dc_texturefrac = sampler.uv_pos;
			draw1column();

			uint64_t step64 = sampler.uv_step;
			uint64_t pos64 = sampler.uv_pos;
			sampler.uv_pos = (uint32_t)(pos64 + step64 * count);
		}
		else
		{
			uint32_t uv_pos = sampler.uv_pos;

			uint32_t left = y2 - y1;
			while (left > 0)
			{
				uint32_t available = sampler.uv_max - uv_pos;
				uint32_t next_uv_wrap = available / sampler.uv_step;
				if (available % sampler.uv_step != 0)
					next_uv_wrap++;
				uint32_t count = MIN(left, next_uv_wrap);

				dc_source = sampler.source;
				dc_source2 = sampler.source2;
				dc_texturefracx = sampler.texturefracx;
				dc_dest = (ylookup[y1] + x) + dc_destorg;
				dc_count = count;
				dc_iscale = sampler.uv_step;
				dc_texturefrac = uv_pos;
				draw1column();

				left -= count;
				uv_pos += sampler.uv_step * count;
				if (uv_pos >= sampler.uv_max)
					uv_pos -= sampler.uv_max;
			}

			sampler.uv_pos = uv_pos;
		}
	}
}

// Draw four columns with support for non-power-of-two ranges
void wallscan_drawcol4(int x, int y1, int y2, WallscanSampler *sampler, void(*draw4columns)())
{
	if (r_swtruecolor)
	{
		int count = y2 - y1;
		for (int i = 0; i < 4; i++)
		{
			bufplce[i] = sampler[i].source;
			bufplce2[i] = sampler[i].source2;
			buftexturefracx[i] = sampler[i].texturefracx;
			bufheight[i] = sampler[i].height;
			vplce[i] = sampler[i].uv_pos;
			vince[i] = sampler[i].uv_step;

			uint64_t step64 = sampler[i].uv_step;
			uint64_t pos64 = sampler[i].uv_pos;
			sampler[i].uv_pos = (uint32_t)(pos64 + step64 * count);
		}
		dc_dest = (ylookup[y1] + x) * 4 + dc_destorg;
		dc_count = count;
		draw4columns();
	}
	else
	{
		if (sampler[0].uv_max == 0) // power of two, no wrap handling needed
		{
			int count = y2 - y1;
			for (int i = 0; i < 4; i++)
			{
				bufplce[i] = sampler[i].source;
				bufplce2[i] = sampler[i].source2;
				buftexturefracx[i] = sampler[i].texturefracx;
				vplce[i] = sampler[i].uv_pos;
				vince[i] = sampler[i].uv_step;

				uint64_t step64 = sampler[i].uv_step;
				uint64_t pos64 = sampler[i].uv_pos;
				sampler[i].uv_pos = (uint32_t)(pos64 + step64 * count);
			}
			dc_dest = (ylookup[y1] + x) + dc_destorg;
			dc_count = count;
			draw4columns();
		}
		else
		{
			dc_dest = (ylookup[y1] + x) + dc_destorg;
			for (int i = 0; i < 4; i++)
			{
				bufplce[i] = sampler[i].source;
				bufplce2[i] = sampler[i].source2;
				buftexturefracx[i] = sampler[i].texturefracx;
			}

			uint32_t left = y2 - y1;
			while (left > 0)
			{
				// Find which column wraps first
				uint32_t count = left;
				for (int i = 0; i < 4; i++)
				{
					uint32_t available = sampler[i].uv_max - sampler[i].uv_pos;
					uint32_t next_uv_wrap = available / sampler[i].uv_step;
					if (available % sampler[i].uv_step != 0)
						next_uv_wrap++;
					count = MIN(next_uv_wrap, count);
				}

				// Draw until that column wraps
				for (int i = 0; i < 4; i++)
				{
					vplce[i] = sampler[i].uv_pos;
					vince[i] = sampler[i].uv_step;
				}
				dc_count = count;
				draw4columns();

				// Wrap the uv position
				for (int i = 0; i < 4; i++)
				{
					sampler[i].uv_pos += sampler[i].uv_step * count;
					if (sampler[i].uv_pos >= sampler[i].uv_max)
						sampler[i].uv_pos -= sampler[i].uv_max;
				}

				left -= count;
			}
		}
	}
}

typedef DWORD(*Draw1ColumnFuncPtr)();
typedef void(*Draw4ColumnsFuncPtr)();

void wallscan_any(
	int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat,
	const BYTE *(*getcol)(FTexture *tex, int x),
	void(setupwallscan(int bits, Draw1ColumnFuncPtr &draw1, Draw4ColumnsFuncPtr &draw2)))
{
	if (rw_pic->UseType == FTexture::TEX_Null)
		return;

	fixed_t xoffset = rw_offset;
	rw_pic->GetHeight(); // To ensure that rw_pic->HeightBits has been set

	DWORD(*draw1column)();
	void(*draw4columns)();
	setupwallscan(r_swtruecolor ? FRACBITS : 32 - rw_pic->HeightBits, draw1column, draw4columns);

	bool fixed = (fixedcolormap != NULL || fixedlightlev >= 0);
	if (fixed)
	{
		palookupoffse[0] = dc_colormap;
		palookupoffse[1] = dc_colormap;
		palookupoffse[2] = dc_colormap;
		palookupoffse[3] = dc_colormap;
		palookuplight[0] = 0;
		palookuplight[1] = 0;
		palookuplight[2] = 0;
		palookuplight[3] = 0;
	}

	if (fixedcolormap)
		R_SetColorMapLight(fixedcolormap, 0, 0);
	else
		R_SetColorMapLight(basecolormap, 0, 0);

	float light = rw_light;

	// Calculate where 4 column alignment begins and ends:
	int aligned_x1 = clamp((x1 + 3) / 4 * 4, x1, x2);
	int aligned_x2 = clamp(x2 / 4 * 4, x1, x2);

	// First unaligned columns:
	for (int x = x1; x < aligned_x1; x++, light += rw_lightstep)
	{
		int y1 = uwal[x];
		int y2 = dwal[x];
		if (y2 <= y1)
			continue;

		if (!fixed)
			R_SetColorMapLight(basecolormap, light, wallshade);

		WallscanSampler sampler(y1, swal[x], yrepeat, lwal[x] + xoffset, rw_pic, getcol);
		wallscan_drawcol1(x, y1, y2, sampler, draw1column);
	}

	// The aligned columns
	for (int x = aligned_x1; x < aligned_x2; x += 4)
	{
		// Find y1, y2, light and uv values for four columns:
		int y1[4] = { uwal[x], uwal[x + 1], uwal[x + 2], uwal[x + 3] };
		int y2[4] = { dwal[x], dwal[x + 1], dwal[x + 2], dwal[x + 3] };

		float lights[4];
		for (int i = 0; i < 4; i++)
		{
			lights[i] = light;
			light += rw_lightstep;
		}

		WallscanSampler sampler[4];
		for (int i = 0; i < 4; i++)
			sampler[i] = WallscanSampler(y1[i], swal[x + i], yrepeat, lwal[x + i] + xoffset, rw_pic, getcol);

		// Figure out where we vertically can start and stop drawing 4 columns in one go
		int middle_y1 = y1[0];
		int middle_y2 = y2[0];
		for (int i = 1; i < 4; i++)
		{
			middle_y1 = MAX(y1[i], middle_y1);
			middle_y2 = MIN(y2[i], middle_y2);
		}

		// If we got an empty column in our set we cannot draw 4 columns in one go:
		bool empty_column_in_set = false;
		int bilinear_count = 0;
		for (int i = 0; i < 4; i++)
		{
			if (y2[i] <= y1[i])
				empty_column_in_set = true;
			if (sampler[i].source2)
				bilinear_count++;
		}

		if (empty_column_in_set || middle_y2 <= middle_y1 || (bilinear_count > 0  && bilinear_count < 4))
		{
			for (int i = 0; i < 4; i++)
			{
				if (y2[i] <= y1[i])
					continue;

				if (!fixed)
					R_SetColorMapLight(basecolormap, lights[i], wallshade);
				wallscan_drawcol1(x + i, y1[i], y2[i], sampler[i], draw1column);
			}
			continue;
		}

		// Draw the first rows where not all 4 columns are active
		for (int i = 0; i < 4; i++)
		{
			if (!fixed)
				R_SetColorMapLight(basecolormap, lights[i], wallshade);

			if (y1[i] < middle_y1)
				wallscan_drawcol1(x + i, y1[i], middle_y1, sampler[i], draw1column);
		}

		// Draw the area where all 4 columns are active
		if (!fixed)
		{
			for (int i = 0; i < 4; i++)
			{
				if (r_swtruecolor)
				{
					palookupoffse[i] = basecolormap->Maps;
					palookuplight[i] = LIGHTSCALE(lights[i], wallshade);
				}
				else
				{
					palookupoffse[i] = basecolormap->Maps + (GETPALOOKUP(lights[i], wallshade) << COLORMAPSHIFT);
					palookuplight[i] = 0;
				}
			}
		}
		wallscan_drawcol4(x, middle_y1, middle_y2, sampler, draw4columns);

		// Draw the last rows where not all 4 columns are active
		for (int i = 0; i < 4; i++)
		{
			if (!fixed)
				R_SetColorMapLight(basecolormap, lights[i], wallshade);

			if (middle_y2 < y2[i])
				wallscan_drawcol1(x + i, middle_y2, y2[i], sampler[i], draw1column);
		}
	}

	// The last unaligned columns:
	for (int x = aligned_x2; x < x2; x++, light += rw_lightstep)
	{
		int y1 = uwal[x];
		int y2 = dwal[x];
		if (y2 <= y1)
			continue;

		if (!fixed)
			R_SetColorMapLight(basecolormap, light, wallshade);

		WallscanSampler sampler(y1, swal[x], yrepeat, lwal[x] + xoffset, rw_pic, getcol);
		wallscan_drawcol1(x, y1, y2, sampler, draw1column);
	}

	NetUpdate ();
}

void wallscan(int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat, const BYTE *(*getcol)(FTexture *tex, int x))
{
	wallscan_any(x1, x2, uwal, dwal, swal, lwal, yrepeat, getcol, [](int bits, Draw1ColumnFuncPtr &line1, Draw4ColumnsFuncPtr &line4)
	{
		setupvline(bits);
		line1 = dovline1;
		line4 = dovline4;
	});
}

void maskwallscan(int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat, const BYTE *(*getcol)(FTexture *tex, int x))
{
	if (!rw_pic->bMasked) // Textures that aren't masked can use the faster wallscan.
	{
		wallscan(x1, x2, uwal, dwal, swal, lwal, yrepeat, getcol);
	}
	else
	{
		wallscan_any(x1, x2, uwal, dwal, swal, lwal, yrepeat, getcol, [](int bits, Draw1ColumnFuncPtr &line1, Draw4ColumnsFuncPtr &line4)
		{
			setupmvline(bits);
			line1 = domvline1;
			line4 = domvline4;
		});
	}
}

void transmaskwallscan(int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat, const BYTE *(*getcol)(FTexture *tex, int x))
{
	static fixed_t(*tmvline1)();
	static void(*tmvline4)();
	if (!R_GetTransMaskDrawers(&tmvline1, &tmvline4))
	{
		// The current translucency is unsupported, so draw with regular maskwallscan instead.
		maskwallscan(x1, x2, uwal, dwal, swal, lwal, yrepeat, getcol);
	}
	else
	{
		wallscan_any(x1, x2, uwal, dwal, swal, lwal, yrepeat, getcol, [](int bits, Draw1ColumnFuncPtr &line1, Draw4ColumnsFuncPtr &line4)
		{
			setuptmvline(bits);
			line1 = reinterpret_cast<DWORD(*)()>(tmvline1);
			line4 = tmvline4;
		});
	}
}

void wallscan_striped (int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat)
{
	FDynamicColormap *startcolormap = basecolormap;
	int startshade = wallshade;
	bool fogginess = foggy;

	short most1[MAXWIDTH], most2[MAXWIDTH], most3[MAXWIDTH];
	short *up, *down;

	up = uwal;
	down = most1;

	assert(WallC.sx1 <= x1);
	assert(WallC.sx2 >= x2);

	// kg3D - fake floors instead of zdoom light list
	for (unsigned int i = 0; i < frontsector->e->XFloor.lightlist.Size(); i++)
	{
		int j = WallMost (most3, frontsector->e->XFloor.lightlist[i].plane, &WallC);
		if (j != 3)
		{
			for (int j = x1; j < x2; ++j)
			{
				down[j] = clamp (most3[j], up[j], dwal[j]);
			}
			wallscan (x1, x2, up, down, swal, lwal, yrepeat);
			up = down;
			down = (down == most1) ? most2 : most1;
		}

		lightlist_t *lit = &frontsector->e->XFloor.lightlist[i];
		basecolormap = lit->extra_colormap;
		wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(fogginess,
			*lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
 	}

	wallscan (x1, x2, up, dwal, swal, lwal, yrepeat);
	basecolormap = startcolormap;
	wallshade = startshade;
}

static void call_wallscan(int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat, bool mask)
{
	if (mask)
	{
		if (colfunc == basecolfunc)
		{
			maskwallscan(x1, x2, uwal, dwal, swal, lwal, yrepeat);
		}
		else
		{
			transmaskwallscan(x1, x2, uwal, dwal, swal, lwal, yrepeat);
		}
	}
	else
	{
		if (fixedcolormap != NULL || fixedlightlev >= 0 || !(frontsector->e && frontsector->e->XFloor.lightlist.Size()))
		{
			wallscan(x1, x2, uwal, dwal, swal, lwal, yrepeat);
		}
		else
		{
			wallscan_striped(x1, x2, uwal, dwal, swal, lwal, yrepeat);
		}
	}
}

//=============================================================================
//
// wallscan_np2
//
// This is a wrapper around wallscan that helps it tile textures whose heights
// are not powers of 2. It divides the wall into texture-sized strips and calls
// wallscan for each of those. Since only one repetition of the texture fits
// in each strip, wallscan will not tile.
//
//=============================================================================

void wallscan_np2(int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat, double top, double bot, bool mask)
{
	if (!r_np2)
	{
		call_wallscan(x1, x2, uwal, dwal, swal, lwal, yrepeat, mask);
	}
	else
	{
		short most1[MAXWIDTH], most2[MAXWIDTH], most3[MAXWIDTH];
		short *up, *down;
		double texheight = rw_pic->GetHeight();
		double partition;
		double scaledtexheight = texheight / yrepeat;

		if (yrepeat >= 0)
		{ // normal orientation: draw strips from top to bottom
			partition = top - fmod(top - dc_texturemid / yrepeat - ViewPos.Z, scaledtexheight);
			if (partition == top)
			{
				partition -= scaledtexheight;
			}
			up = uwal;
			down = most1;
			dc_texturemid = (partition - ViewPos.Z) * yrepeat + texheight;
			while (partition > bot)
			{
				int j = OWallMost(most3, partition - ViewPos.Z, &WallC);
				if (j != 3)
				{
					for (int j = x1; j < x2; ++j)
					{
						down[j] = clamp(most3[j], up[j], dwal[j]);
					}
					call_wallscan(x1, x2, up, down, swal, lwal, yrepeat, mask);
					up = down;
					down = (down == most1) ? most2 : most1;
				}
				partition -= scaledtexheight;
				dc_texturemid -= texheight;
 			}
			call_wallscan(x1, x2, up, dwal, swal, lwal, yrepeat, mask);
		}
		else
		{ // upside down: draw strips from bottom to top
			partition = bot - fmod(bot - dc_texturemid / yrepeat - ViewPos.Z, scaledtexheight);
			up = most1;
			down = dwal;
			dc_texturemid = (partition - ViewPos.Z) * yrepeat + texheight;
			while (partition < top)
			{
				int j = OWallMost(most3, partition - ViewPos.Z, &WallC);
				if (j != 12)
				{
					for (int j = x1; j < x2; ++j)
					{
						up[j] = clamp(most3[j], uwal[j], down[j]);
					}
					call_wallscan(x1, x2, up, down, swal, lwal, yrepeat, mask);
					down = up;
					up = (up == most1) ? most2 : most1;
				}
				partition -= scaledtexheight;
				dc_texturemid -= texheight;
 			}
			call_wallscan(x1, x2, uwal, down, swal, lwal, yrepeat, mask);
		}
	}
}

static void wallscan_np2_ds(drawseg_t *ds, int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat)
{
	if (rw_pic->GetHeight() != 1 << rw_pic->HeightBits)
	{
		double frontcz1 = ds->curline->frontsector->ceilingplane.ZatPoint(ds->curline->v1);
		double frontfz1 = ds->curline->frontsector->floorplane.ZatPoint(ds->curline->v1);
		double frontcz2 = ds->curline->frontsector->ceilingplane.ZatPoint(ds->curline->v2);
		double frontfz2 = ds->curline->frontsector->floorplane.ZatPoint(ds->curline->v2);
		double top = MAX(frontcz1, frontcz2);
		double bot = MIN(frontfz1, frontfz2);
		if (fake3D & FAKE3D_CLIPTOP)
		{
			top = MIN(top, sclipTop);
		}
		if (fake3D & FAKE3D_CLIPBOTTOM)
		{
			bot = MAX(bot, sclipBottom);
		}
		wallscan_np2(x1, x2, uwal, dwal, swal, lwal, yrepeat, top, bot, true);
	}
	else
	{
		call_wallscan(x1, x2, uwal, dwal, swal, lwal, yrepeat, true);
	}
}

//
// R_RenderSegLoop
// Draws zero, one, or two textures for walls.
// Can draw or mark the starting pixel of floor and ceiling textures.
// CALLED: CORE LOOPING ROUTINE.
//
// [RH] Rewrote this to use Build's wallscan, so it's quite far
// removed from the original Doom routine.
//

void R_RenderSegLoop ()
{
	int x1 = rw_x;
	int x2 = rw_stopx;
	int x;
	double xscale;
	double yscale;
	fixed_t xoffset = rw_offset;

	if (fixedlightlev >= 0)
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : basecolormap, 0, FIXEDLIGHT2SHADE(fixedlightlev));
	else if (fixedcolormap != NULL)
		R_SetColorMapLight(fixedcolormap, 0, 0);

	// clip wall to the floor and ceiling
	for (x = x1; x < x2; ++x)
	{
		if (walltop[x] < ceilingclip[x])
		{
			walltop[x] = ceilingclip[x];
		}
		if (wallbottom[x] > floorclip[x])
		{
			wallbottom[x] = floorclip[x];
		}
	}

	// mark ceiling areas
	if (markceiling)
	{
		for (x = x1; x < x2; ++x)
		{
			short top = (fakeFloor && fake3D & 2) ? fakeFloor->ceilingclip[x] : ceilingclip[x];
			short bottom = MIN (walltop[x], floorclip[x]);
			if (top < bottom)
			{
				ceilingplane->top[x] = top;
				ceilingplane->bottom[x] = bottom;
			}
		}
	}

	// mark floor areas
	if (markfloor)
	{
		for (x = x1; x < x2; ++x)
		{
			short top = MAX (wallbottom[x], ceilingclip[x]);
			short bottom = (fakeFloor && fake3D & 1) ? fakeFloor->floorclip[x] : floorclip[x];
			if (top < bottom)
			{
				assert (bottom <= viewheight);
				floorplane->top[x] = top;
				floorplane->bottom[x] = bottom;
			}
		}
	}

	// kg3D - fake planes clipping
	if (fake3D & FAKE3D_REFRESHCLIP)
	{
		if (fake3D & FAKE3D_CLIPBOTFRONT)
		{
			memcpy (fakeFloor->floorclip+x1, wallbottom+x1, (x2-x1)*sizeof(short));
		}
		else
		{
			for (x = x1; x < x2; ++x)
			{
				walllower[x] = MIN (MAX (walllower[x], ceilingclip[x]), wallbottom[x]);
			}
			memcpy (fakeFloor->floorclip+x1, walllower+x1, (x2-x1)*sizeof(short));
		}
		if (fake3D & FAKE3D_CLIPTOPFRONT)
		{
			memcpy (fakeFloor->ceilingclip+x1, walltop+x1, (x2-x1)*sizeof(short));
		}
		else
		{
			for (x = x1; x < x2; ++x)
			{
				wallupper[x] = MAX (MIN (wallupper[x], floorclip[x]), walltop[x]);
			}
			memcpy (fakeFloor->ceilingclip+x1, wallupper+x1, (x2-x1)*sizeof(short));
		}
	}
	if(fake3D & 7) return;

	// draw the wall tiers
	if (midtexture)
	{ // one sided line
		if (midtexture->UseType != FTexture::TEX_Null && viewactive)
		{
			dc_texturemid = rw_midtexturemid;
			rw_pic = midtexture;
			xscale = rw_pic->Scale.X * rw_midtexturescalex;
			yscale = rw_pic->Scale.Y * rw_midtexturescaley;
			if (xscale != lwallscale)
			{
				PrepLWall (lwall, curline->sidedef->TexelLength*xscale, WallC.sx1, WallC.sx2);
				lwallscale = xscale;
			}
			if (midtexture->bWorldPanning)
			{
				rw_offset = xs_RoundToInt(rw_offset_mid * xscale);
			}
			else
			{
				rw_offset = rw_offset_mid;
			}
			if (xscale < 0)
			{
				rw_offset = -rw_offset;
			}
			if (rw_pic->GetHeight() != 1 << rw_pic->HeightBits)
			{
				wallscan_np2(x1, x2, walltop, wallbottom, swall, lwall, yscale, MAX(rw_frontcz1, rw_frontcz2), MIN(rw_frontfz1, rw_frontfz2), false);
			}
			else
			{
				call_wallscan(x1, x2, walltop, wallbottom, swall, lwall, yscale, false);
			}
		}
		clearbufshort (ceilingclip+x1, x2-x1, viewheight);
		clearbufshort (floorclip+x1, x2-x1, 0xffff);
	}
	else
	{ // two sided line
		if (toptexture != NULL && toptexture->UseType != FTexture::TEX_Null)
		{ // top wall
			for (x = x1; x < x2; ++x)
			{
				wallupper[x] = MAX (MIN (wallupper[x], floorclip[x]), walltop[x]);
			}
			if (viewactive)
			{
				dc_texturemid = rw_toptexturemid;
				rw_pic = toptexture;
				xscale = rw_pic->Scale.X * rw_toptexturescalex;
				yscale = rw_pic->Scale.Y * rw_toptexturescaley;
				if (xscale != lwallscale)
				{
					PrepLWall (lwall, curline->sidedef->TexelLength*xscale, WallC.sx1, WallC.sx2);
					lwallscale = xscale;
				}
				if (toptexture->bWorldPanning)
				{
					rw_offset = xs_RoundToInt(rw_offset_top * xscale);
				}
				else
				{
					rw_offset = rw_offset_top;
				}
				if (xscale < 0)
				{
					rw_offset = -rw_offset;
				}
				if (rw_pic->GetHeight() != 1 << rw_pic->HeightBits)
				{
					wallscan_np2(x1, x2, walltop, wallupper, swall, lwall, yscale, MAX(rw_frontcz1, rw_frontcz2), MIN(rw_backcz1, rw_backcz2), false);
				}
				else
				{
					call_wallscan(x1, x2, walltop, wallupper, swall, lwall, yscale, false);
				}
			}
			memcpy (ceilingclip+x1, wallupper+x1, (x2-x1)*sizeof(short));
		}
		else if (markceiling)
		{ // no top wall
			memcpy (ceilingclip+x1, walltop+x1, (x2-x1)*sizeof(short));
		}

		
		if (bottomtexture != NULL && bottomtexture->UseType != FTexture::TEX_Null)
		{ // bottom wall
			for (x = x1; x < x2; ++x)
			{
				walllower[x] = MIN (MAX (walllower[x], ceilingclip[x]), wallbottom[x]);
			}
			if (viewactive)
			{
				dc_texturemid = rw_bottomtexturemid;
				rw_pic = bottomtexture;
				xscale = rw_pic->Scale.X * rw_bottomtexturescalex;
				yscale = rw_pic->Scale.Y * rw_bottomtexturescaley;
				if (xscale != lwallscale)
				{
					PrepLWall (lwall, curline->sidedef->TexelLength*xscale, WallC.sx1, WallC.sx2);
					lwallscale = xscale;
				}
				if (bottomtexture->bWorldPanning)
				{
					rw_offset = xs_RoundToInt(rw_offset_bottom * xscale);
				}
				else
				{
					rw_offset = rw_offset_bottom;
				}
				if (xscale < 0)
				{
					rw_offset = -rw_offset;
				}
				if (rw_pic->GetHeight() != 1 << rw_pic->HeightBits)
				{
					wallscan_np2(x1, x2, walllower, wallbottom, swall, lwall, yscale, MAX(rw_backfz1, rw_backfz2), MIN(rw_frontfz1, rw_frontfz2), false);
				}
				else
				{
					call_wallscan(x1, x2, walllower, wallbottom, swall, lwall, yscale, false);
				}
			}
			memcpy (floorclip+x1, walllower+x1, (x2-x1)*sizeof(short));
		}
		else if (markfloor)
		{ // no bottom wall
			memcpy (floorclip+x1, wallbottom+x1, (x2-x1)*sizeof(short));
		}
	}
	rw_offset = xoffset;
}

void R_NewWall (bool needlights)
{
	double rowoffset;
	double yrepeat;

	rw_markportal = false;

	sidedef = curline->sidedef;
	linedef = curline->linedef;

	// mark the segment as visible for auto map
	if (!r_dontmaplines) linedef->flags |= ML_MAPPED;

	midtexture = toptexture = bottomtexture = 0;

	if (sidedef == linedef->sidedef[0] &&
		(linedef->special == Line_Mirror && r_drawmirrors)) // [ZZ] compatibility with r_drawmirrors cvar that existed way before portals
	{
		markfloor = markceiling = true; // act like a one-sided wall here (todo: check how does this work with transparency)
		rw_markportal = true;
	}
	else if (backsector == NULL)
	{
		// single sided line
		// a single sided line is terminal, so it must mark ends
		markfloor = markceiling = true;
		// [RH] Horizon lines do not need to be textured
		if (linedef->isVisualPortal())
		{
			rw_markportal = true;
		}
		else if (linedef->special != Line_Horizon)
		{
			midtexture = TexMan(sidedef->GetTexture(side_t::mid), true);
			rw_offset_mid = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::mid));
			rowoffset = sidedef->GetTextureYOffset(side_t::mid);
			rw_midtexturescalex = sidedef->GetTextureXScale(side_t::mid);
			rw_midtexturescaley = sidedef->GetTextureYScale(side_t::mid);
			yrepeat = midtexture->Scale.Y * rw_midtexturescaley;
			if (yrepeat >= 0)
			{ // normal orientation
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // bottom of texture at bottom
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat + midtexture->GetHeight();
				}
				else
				{ // top of texture at top
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat;
					if (rowoffset < 0 && midtexture != NULL)
					{
						rowoffset += midtexture->GetHeight();
					}
				}
			}
			else
			{ // upside down
				rowoffset = -rowoffset;
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // top of texture at bottom
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat;
				}
				else
				{ // bottom of texture at top
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat + midtexture->GetHeight();
				}
			}
			if (midtexture->bWorldPanning)
			{
				rw_midtexturemid += rowoffset * yrepeat;
			}
			else
			{
				// rowoffset is added outside the multiply so that it positions the texture
				// by texels instead of world units.
				rw_midtexturemid += rowoffset;
			}
		}
	}
	else
	{ // two-sided line
		// hack to allow height changes in outdoor areas

		rw_frontlowertop = frontsector->GetPlaneTexZ(sector_t::ceiling);

		if (frontsector->GetTexture(sector_t::ceiling) == skyflatnum &&
			backsector->GetTexture(sector_t::ceiling) == skyflatnum)
		{
			if (rw_havehigh)
			{ // front ceiling is above back ceiling
				memcpy (&walltop[WallC.sx1], &wallupper[WallC.sx1], (WallC.sx2 - WallC.sx1)*sizeof(walltop[0]));
				rw_havehigh = false;
			}
			else if (rw_havelow && frontsector->ceilingplane != backsector->ceilingplane)
			{ // back ceiling is above front ceiling
				// The check for rw_havelow is not Doom-compliant, but it avoids HoM that
				// would otherwise occur because there is space made available for this
				// wall but nothing to draw for it.
				// Recalculate walltop so that the wall is clipped by the back sector's
				// ceiling instead of the front sector's ceiling.
				WallMost (walltop, backsector->ceilingplane, &WallC);
			}
			// Putting sky ceilings on the front and back of a line alters the way unpegged
			// positioning works.
			rw_frontlowertop = backsector->GetPlaneTexZ(sector_t::ceiling);
		}

		if (linedef->isVisualPortal())
		{
			markceiling = markfloor = true;
		}
		else if ((rw_backcz1 <= rw_frontfz1 && rw_backcz2 <= rw_frontfz2) ||
				 (rw_backfz1 >= rw_frontcz1 && rw_backfz2 >= rw_frontcz2))
		{
			// closed door
			markceiling = markfloor = true;
		}
		else
		{
			markfloor = rw_mustmarkfloor
				|| backsector->floorplane != frontsector->floorplane
				|| backsector->lightlevel != frontsector->lightlevel
				|| backsector->GetTexture(sector_t::floor) != frontsector->GetTexture(sector_t::floor)
				|| backsector->GetPlaneLight(sector_t::floor) != frontsector->GetPlaneLight(sector_t::floor)

				// killough 3/7/98: Add checks for (x,y) offsets
				|| backsector->planes[sector_t::floor].xform != frontsector->planes[sector_t::floor].xform
				|| backsector->GetAlpha(sector_t::floor) != frontsector->GetAlpha(sector_t::floor)

				// killough 4/15/98: prevent 2s normals
				// from bleeding through deep water
				|| frontsector->heightsec

				|| backsector->GetVisFlags(sector_t::floor) != frontsector->GetVisFlags(sector_t::floor)

				// [RH] Add checks for colormaps
				|| backsector->ColorMap != frontsector->ColorMap


				// kg3D - add fake lights
				|| (frontsector->e && frontsector->e->XFloor.lightlist.Size())
				|| (backsector->e && backsector->e->XFloor.lightlist.Size())

				|| (sidedef->GetTexture(side_t::mid).isValid() &&
					((linedef->flags & (ML_CLIP_MIDTEX|ML_WRAP_MIDTEX)) ||
					 (sidedef->Flags & (WALLF_CLIP_MIDTEX|WALLF_WRAP_MIDTEX))))
				;

			markceiling = (frontsector->GetTexture(sector_t::ceiling) != skyflatnum ||
				backsector->GetTexture(sector_t::ceiling) != skyflatnum) &&
				(rw_mustmarkceiling
				|| backsector->ceilingplane != frontsector->ceilingplane
				|| backsector->lightlevel != frontsector->lightlevel
				|| backsector->GetTexture(sector_t::ceiling) != frontsector->GetTexture(sector_t::ceiling)

				// killough 3/7/98: Add checks for (x,y) offsets
				|| backsector->planes[sector_t::ceiling].xform != frontsector->planes[sector_t::ceiling].xform
				|| backsector->GetAlpha(sector_t::ceiling) != frontsector->GetAlpha(sector_t::ceiling)

				// killough 4/15/98: prevent 2s normals
				// from bleeding through fake ceilings
				|| (frontsector->heightsec && frontsector->GetTexture(sector_t::ceiling) != skyflatnum)

				|| backsector->GetPlaneLight(sector_t::ceiling) != frontsector->GetPlaneLight(sector_t::ceiling)
				|| backsector->GetFlags(sector_t::ceiling) != frontsector->GetFlags(sector_t::ceiling)

				// [RH] Add check for colormaps
				|| backsector->ColorMap != frontsector->ColorMap

				// kg3D - add fake lights
				|| (frontsector->e && frontsector->e->XFloor.lightlist.Size())
				|| (backsector->e && backsector->e->XFloor.lightlist.Size())

				|| (sidedef->GetTexture(side_t::mid).isValid() &&
					((linedef->flags & (ML_CLIP_MIDTEX|ML_WRAP_MIDTEX)) ||
					(sidedef->Flags & (WALLF_CLIP_MIDTEX|WALLF_WRAP_MIDTEX))))
				);
		}

		if (rw_havehigh)
		{ // top texture
			toptexture = TexMan(sidedef->GetTexture(side_t::top), true);

			rw_offset_top = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::top));
			rowoffset = sidedef->GetTextureYOffset(side_t::top);
			rw_toptexturescalex =sidedef->GetTextureXScale(side_t::top);
			rw_toptexturescaley =sidedef->GetTextureYScale(side_t::top);
			yrepeat = toptexture->Scale.Y * rw_toptexturescaley;
			if (yrepeat >= 0)
			{ // normal orientation
				if (linedef->flags & ML_DONTPEGTOP)
				{ // top of texture at top
					rw_toptexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat;
					if (rowoffset < 0 && toptexture != NULL)
					{
						rowoffset += toptexture->GetHeight();
					}
				}
				else
				{ // bottom of texture at bottom
					rw_toptexturemid = (backsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat + toptexture->GetHeight();
				}
			}
			else
			{ // upside down
				rowoffset = -rowoffset;
				if (linedef->flags & ML_DONTPEGTOP)
				{ // bottom of texture at top
					rw_toptexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat + toptexture->GetHeight();
				}
				else
				{ // top of texture at bottom
					rw_toptexturemid = (backsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat;
				}
			}
			if (toptexture->bWorldPanning)
			{
				rw_toptexturemid += rowoffset * yrepeat;
			}
			else
			{
				rw_toptexturemid += rowoffset;
			}
		}
		if (rw_havelow)
		{ // bottom texture
			bottomtexture = TexMan(sidedef->GetTexture(side_t::bottom), true);

			rw_offset_bottom = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::bottom));
			rowoffset = sidedef->GetTextureYOffset(side_t::bottom);
			rw_bottomtexturescalex = sidedef->GetTextureXScale(side_t::bottom);
			rw_bottomtexturescaley = sidedef->GetTextureYScale(side_t::bottom);
			yrepeat = bottomtexture->Scale.Y * rw_bottomtexturescaley;
			if (yrepeat >= 0)
			{ // normal orientation
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // bottom of texture at bottom
					rw_bottomtexturemid = (rw_frontlowertop - ViewPos.Z) * yrepeat;
				}
				else
				{ // top of texture at top
					rw_bottomtexturemid = (backsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat;
					if (rowoffset < 0 && bottomtexture != NULL)
					{
						rowoffset += bottomtexture->GetHeight();
					}
				}
			}
			else
			{ // upside down
				rowoffset = -rowoffset;
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // top of texture at bottom
					rw_bottomtexturemid = (rw_frontlowertop - ViewPos.Z) * yrepeat;
				}
				else
				{ // bottom of texture at top
					rw_bottomtexturemid = (backsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat + bottomtexture->GetHeight();
				}
			}
			if (bottomtexture->bWorldPanning)
			{
				rw_bottomtexturemid += rowoffset * yrepeat;
			}
			else
			{
				rw_bottomtexturemid += rowoffset;
			}
		}
		rw_markportal = linedef->isVisualPortal();
	}

	// if a floor / ceiling plane is on the wrong side of the view plane,
	// it is definitely invisible and doesn't need to be marked.

	// killough 3/7/98: add deep water check
	if (frontsector->GetHeightSec() == NULL)
	{
		int planeside;

		planeside = frontsector->floorplane.PointOnSide(ViewPos);
		if (frontsector->floorplane.fC() < 0)	// 3D floors have the floor backwards
			planeside = -planeside;
		if (planeside <= 0)		// above view plane
			markfloor = false;

		if (frontsector->GetTexture(sector_t::ceiling) != skyflatnum)
		{
			planeside = frontsector->ceilingplane.PointOnSide(ViewPos);
			if (frontsector->ceilingplane.fC() > 0)	// 3D floors have the ceiling backwards
				planeside = -planeside;
			if (planeside <= 0)		// below view plane
				markceiling = false;
		}
	}

	FTexture *midtex = TexMan(sidedef->GetTexture(side_t::mid), true);

	segtextured = midtex != NULL || toptexture != NULL || bottomtexture != NULL;

	// calculate light table
	if (needlights && (segtextured || (backsector && IsFogBoundary(frontsector, backsector))))
	{
		lwallscale =
			midtex ? (midtex->Scale.X * sidedef->GetTextureXScale(side_t::mid)) :
			toptexture ? (toptexture->Scale.X * sidedef->GetTextureXScale(side_t::top)) :
			bottomtexture ? (bottomtexture->Scale.X * sidedef->GetTextureXScale(side_t::bottom)) :
			1.;

		PrepWall (swall, lwall, sidedef->TexelLength * lwallscale, WallC.sx1, WallC.sx2);

		if (fixedcolormap == NULL && fixedlightlev < 0)
		{
			wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, frontsector->lightlevel)
				+ r_actualextralight);
			GlobVis = r_WallVisibility;
			rw_lightleft = float (GlobVis / WallC.sz1);
			rw_lightstep = float((GlobVis / WallC.sz2 - rw_lightleft) / (WallC.sx2 - WallC.sx1));
		}
		else
		{
			rw_lightleft = 1;
			rw_lightstep = 0;
		}
	}
}


//
// R_CheckDrawSegs
//

void R_CheckDrawSegs ()
{
	if (ds_p == &drawsegs[MaxDrawSegs])
	{ // [RH] Grab some more drawsegs
		size_t newdrawsegs = MaxDrawSegs ? MaxDrawSegs*2 : 32;
		ptrdiff_t firstofs = firstdrawseg - drawsegs;
		drawsegs = (drawseg_t *)M_Realloc (drawsegs, newdrawsegs * sizeof(drawseg_t));
		firstdrawseg = drawsegs + firstofs;
		ds_p = drawsegs + MaxDrawSegs;
		MaxDrawSegs = newdrawsegs;
		DPrintf (DMSG_NOTIFY, "MaxDrawSegs increased to %zu\n", MaxDrawSegs);
	}
}

//
// R_CheckOpenings
//

ptrdiff_t R_NewOpening (ptrdiff_t len)
{
	ptrdiff_t res = lastopening;
	len = (len + 1) & ~1;	// only return DWORD aligned addresses because some code stores fixed_t's and floats in openings... 
	lastopening += len;
	if ((size_t)lastopening > maxopenings)
	{
		do
			maxopenings = maxopenings ? maxopenings*2 : 16384;
		while ((size_t)lastopening > maxopenings);
		openings = (short *)M_Realloc (openings, maxopenings * sizeof(*openings));
		DPrintf (DMSG_NOTIFY, "MaxOpenings increased to %zu\n", maxopenings);
	}
	return res;
}


//
// R_StoreWallRange
// A wall segment will be drawn between start and stop pixels (inclusive).
//

void R_StoreWallRange (int start, int stop)
{
	int i;
	bool maskedtexture = false;

#ifdef RANGECHECK
	if (start >= viewwidth || start >= stop)
		I_FatalError ("Bad R_StoreWallRange: %i to %i", start , stop);
#endif

	// don't overflow and crash
	R_CheckDrawSegs ();
	
	if (!rw_prepped)
	{
		rw_prepped = true;
		R_NewWall (true);
	}

	rw_offset = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::mid));
	rw_light = rw_lightleft + rw_lightstep * (start - WallC.sx1);

	ds_p->CurrentPortalUniq = CurrentPortalUniq;
	ds_p->sx1 = WallC.sx1;
	ds_p->sx2 = WallC.sx2;
	ds_p->sz1 = WallC.sz1;
	ds_p->sz2 = WallC.sz2;
	ds_p->cx = WallC.tleft.X;;
	ds_p->cy = WallC.tleft.Y;
	ds_p->cdx = WallC.tright.X - WallC.tleft.X;
	ds_p->cdy = WallC.tright.Y - WallC.tleft.Y;
	ds_p->tmapvals = WallT;
	ds_p->siz1 = 1 / WallC.sz1;
	ds_p->siz2 = 1 / WallC.sz2;
	ds_p->x1 = rw_x = start;
	ds_p->x2 = stop;
	ds_p->curline = curline;
	rw_stopx = stop;
	ds_p->bFogBoundary = false;
	ds_p->bFakeBoundary = false;
	if(fake3D & 7) ds_p->fake = 1;
	else ds_p->fake = 0;

	// killough 1/6/98, 2/1/98: remove limit on openings
	ds_p->sprtopclip = ds_p->sprbottomclip = ds_p->maskedtexturecol = ds_p->bkup = ds_p->swall = -1;

	if (rw_markportal)
	{
		ds_p->silhouette = SIL_BOTH;
	}
	else if (backsector == NULL)
	{
		ds_p->sprtopclip = R_NewOpening (stop - start);
		ds_p->sprbottomclip = R_NewOpening (stop - start);
		clearbufshort (openings + ds_p->sprtopclip, stop-start, viewheight);
		memset (openings + ds_p->sprbottomclip, -1, (stop-start)*sizeof(short));
		ds_p->silhouette = SIL_BOTH;
	}
	else
	{
		// two sided line
		ds_p->silhouette = 0;

		if (rw_frontfz1 > rw_backfz1 || rw_frontfz2 > rw_backfz2 ||
			backsector->floorplane.PointOnSide(ViewPos) < 0)
		{
			ds_p->silhouette = SIL_BOTTOM;
		}

		if (rw_frontcz1 < rw_backcz1 || rw_frontcz2 < rw_backcz2 ||
			backsector->ceilingplane.PointOnSide(ViewPos) < 0)
		{
			ds_p->silhouette |= SIL_TOP;
		}

		// killough 1/17/98: this test is required if the fix
		// for the automap bug (r_bsp.c) is used, or else some
		// sprites will be displayed behind closed doors. That
		// fix prevents lines behind closed doors with dropoffs
		// from being displayed on the automap.
		//
		// killough 4/7/98: make doorclosed external variable

		{
			extern int doorclosed;	// killough 1/17/98, 2/8/98, 4/7/98
			if (doorclosed || (rw_backcz1 <= rw_frontfz1 && rw_backcz2 <= rw_frontfz2))
			{
				ds_p->sprbottomclip = R_NewOpening (stop - start);
				memset (openings + ds_p->sprbottomclip, -1, (stop-start)*sizeof(short));
				ds_p->silhouette |= SIL_BOTTOM;
			}
			if (doorclosed || (rw_backfz1 >= rw_frontcz1 && rw_backfz2 >= rw_frontcz2))
			{						// killough 1/17/98, 2/8/98
				ds_p->sprtopclip = R_NewOpening (stop - start);
				clearbufshort (openings + ds_p->sprtopclip, stop - start, viewheight);
				ds_p->silhouette |= SIL_TOP;
			}
		}

		if(!ds_p->fake && r_3dfloors && backsector->e && backsector->e->XFloor.ffloors.Size()) {
			for(i = 0; i < (int)backsector->e->XFloor.ffloors.Size(); i++) {
				F3DFloor *rover = backsector->e->XFloor.ffloors[i];
				if(rover->flags & FF_RENDERSIDES && (!(rover->flags & FF_INVERTSIDES) || rover->flags & FF_ALLSIDES)) {
					ds_p->bFakeBoundary |= 1;
					break;
				}
			}
		}
		if(!ds_p->fake && r_3dfloors && frontsector->e && frontsector->e->XFloor.ffloors.Size()) {
			for(i = 0; i < (int)frontsector->e->XFloor.ffloors.Size(); i++) {
				F3DFloor *rover = frontsector->e->XFloor.ffloors[i];
				if(rover->flags & FF_RENDERSIDES && (rover->flags & FF_ALLSIDES || rover->flags & FF_INVERTSIDES)) {
					ds_p->bFakeBoundary |= 2;
					break;
				}
			}
		}
		// kg3D - no for fakes
		if(!ds_p->fake)
		// allocate space for masked texture tables, if needed
		// [RH] Don't just allocate the space; fill it in too.
		if ((TexMan(sidedef->GetTexture(side_t::mid), true)->UseType != FTexture::TEX_Null || ds_p->bFakeBoundary || IsFogBoundary (frontsector, backsector)) &&
			(rw_ceilstat != 12 || !sidedef->GetTexture(side_t::top).isValid()) &&
			(rw_floorstat != 3 || !sidedef->GetTexture(side_t::bottom).isValid()) &&
			(WallC.sz1 >= TOO_CLOSE_Z && WallC.sz2 >= TOO_CLOSE_Z))
		{
			float *swal;
			fixed_t *lwal;
			int i;

			maskedtexture = true;

			// kg3D - backup for mid and fake walls
			ds_p->bkup = R_NewOpening(stop - start);
			memcpy(openings + ds_p->bkup, &ceilingclip[start], sizeof(short)*(stop - start));

			ds_p->bFogBoundary = IsFogBoundary (frontsector, backsector);
			if (sidedef->GetTexture(side_t::mid).isValid() || ds_p->bFakeBoundary)
			{
				if(sidedef->GetTexture(side_t::mid).isValid())
					ds_p->bFakeBoundary |= 4; // it is also mid texture

				// note: This should never have used the openings array to store its data!
				ds_p->maskedtexturecol = R_NewOpening ((stop - start) * 2);
				ds_p->swall = R_NewOpening ((stop - start) * 2);

				lwal = (fixed_t *)(openings + ds_p->maskedtexturecol);
				swal = (float *)(openings + ds_p->swall);
				FTexture *pic = TexMan(sidedef->GetTexture(side_t::mid), true);
				double yscale = pic->Scale.X * sidedef->GetTextureYScale(side_t::mid);
				fixed_t xoffset = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::mid));

				if (pic->bWorldPanning)
				{
					xoffset = xs_RoundToInt(xoffset * lwallscale);
				}

				for (i = start; i < stop; i++)
				{
					*lwal++ = lwall[i] + xoffset;
					*swal++ = swall[i];
				}

				double istart = *((float *)(openings + ds_p->swall)) * yscale;
				double iend = *(swal - 1) * yscale;
#if 0
				///This was for avoiding overflow when using fixed point. It might not be needed anymore.
				const double mini = 3 / 65536.0;
				if (istart < mini && istart >= 0) istart = mini;
				if (istart > -mini && istart < 0) istart = -mini;
				if (iend < mini && iend >= 0) iend = mini;
				if (iend > -mini && iend < 0) iend = -mini;
#endif
				istart = 1 / istart;
				iend = 1 / iend;
				ds_p->yscale = (float)yscale;
				ds_p->iscale = (float)istart;
				if (stop - start > 0)
				{
					ds_p->iscalestep = float((iend - istart) / (stop - start));
				}
				else
				{
					ds_p->iscalestep = 0;
				}
			}
			ds_p->light = rw_light;
			ds_p->lightstep = rw_lightstep;

			// Masked midtextures should get the light level from the sector they reference,
			// not from the current subsector, which is what the current wallshade value
			// comes from. We make an exeption for polyobjects, however, since their "home"
			// sector should be whichever one they move into.
			if (curline->sidedef->Flags & WALLF_POLYOBJ)
			{
				ds_p->shade = wallshade;
			}
			else
			{
				ds_p->shade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, curline->frontsector->lightlevel)
					+ r_actualextralight);
			}

			if (ds_p->bFogBoundary || ds_p->maskedtexturecol != -1)
			{
				size_t drawsegnum = ds_p - drawsegs;
				InterestingDrawsegs.Push (drawsegnum);
			}
		}
	}
	
	// render it
	if (markceiling)
	{
		if (ceilingplane)
		{	// killough 4/11/98: add NULL ptr checks
			ceilingplane = R_CheckPlane (ceilingplane, start, stop);
		}
		else
		{
			markceiling = false;
		}
	}
	
	if (markfloor)
	{
		if (floorplane)
		{	// killough 4/11/98: add NULL ptr checks
			floorplane = R_CheckPlane (floorplane, start, stop);
		}
		else
		{
			markfloor = false;
		}
	}

	R_RenderSegLoop ();

	if(fake3D & 7) {
		ds_p++;
		return;
	}

	// save sprite clipping info
	if ( ((ds_p->silhouette & SIL_TOP) || maskedtexture) && ds_p->sprtopclip == -1)
	{
		ds_p->sprtopclip = R_NewOpening (stop - start);
		memcpy (openings + ds_p->sprtopclip, &ceilingclip[start], sizeof(short)*(stop-start));
	}

	if ( ((ds_p->silhouette & SIL_BOTTOM) || maskedtexture) && ds_p->sprbottomclip == -1)
	{
		ds_p->sprbottomclip = R_NewOpening (stop - start);
		memcpy (openings + ds_p->sprbottomclip, &floorclip[start], sizeof(short)*(stop-start));
	}

	if (maskedtexture && curline->sidedef->GetTexture(side_t::mid).isValid())
	{
		ds_p->silhouette |= SIL_TOP | SIL_BOTTOM;
	}

	// [RH] Draw any decals bound to the seg
	// [ZZ] Only if not an active mirror
	if (!rw_markportal)
	{
		for (DBaseDecal *decal = curline->sidedef->AttachedDecals; decal != NULL; decal = decal->WallNext)
		{
			R_RenderDecal (curline->sidedef, decal, ds_p, 0);
		}
	}

	if (rw_markportal)
	{
		PortalDrawseg pds;
		pds.src = curline->linedef;
		pds.dst = curline->linedef->special == Line_Mirror? curline->linedef : curline->linedef->getPortalDestination();
		pds.x1 = ds_p->x1;
		pds.x2 = ds_p->x2;
		pds.len = pds.x2 - pds.x1;
		pds.ceilingclip.Resize(pds.len);
		memcpy(&pds.ceilingclip[0], openings + ds_p->sprtopclip, pds.len*sizeof(*openings));
		pds.floorclip.Resize(pds.len);
		memcpy(&pds.floorclip[0], openings + ds_p->sprbottomclip, pds.len*sizeof(*openings));

		for (int i = 0; i < pds.x2-pds.x1; i++)
		{
			if (pds.ceilingclip[i] < 0)
				pds.ceilingclip[i] = 0;
			if (pds.ceilingclip[i] >= viewheight)
				pds.ceilingclip[i] = viewheight-1;
			if (pds.floorclip[i] < 0)
				pds.floorclip[i] = 0;
			if (pds.floorclip[i] >= viewheight)
				pds.floorclip[i] = viewheight-1;
		}

		pds.mirror = curline->linedef->special == Line_Mirror;
		WallPortals.Push(pds);
	}

	ds_p++;
}

int OWallMost (short *mostbuf, double z, const FWallCoords *wallc)
{
	int bad, ix1, ix2;
	double y, iy1, iy2;
	double s1, s2, s3, s4;

	z = -z;
	s1 = globaluclip * wallc->sz1; s2 = globaluclip * wallc->sz2;
	s3 = globaldclip * wallc->sz1; s4 = globaldclip * wallc->sz2;
	bad = (z<s1)+((z<s2)<<1)+((z>s3)<<2)+((z>s4)<<3);

	if ((bad&3) == 3)
	{ // entire line is above the screen
		memset (&mostbuf[wallc->sx1], 0, (wallc->sx2 - wallc->sx1)*sizeof(mostbuf[0]));
		return bad;
	}

	if ((bad&12) == 12)
	{ // entire line is below the screen
		clearbufshort (&mostbuf[wallc->sx1], wallc->sx2 - wallc->sx1, viewheight);
		return bad;
	}
	ix1 = wallc->sx1; iy1 = wallc->sz1;
	ix2 = wallc->sx2; iy2 = wallc->sz2;
	if (bad & 3)
	{ // the line intersects the top of the screen
		double t = (z-s1) / (s2-s1);
		double inty = wallc->sz1 + t * (wallc->sz2 - wallc->sz1);
		int xcross = xs_RoundToInt(wallc->sx1 + (t * wallc->sz2 * (wallc->sx2 - wallc->sx1)) / inty);

		if ((bad & 3) == 2)
		{ // the right side is above the screen
			if (wallc->sx1 <= xcross) { iy2 = inty; ix2 = xcross; }
			if (wallc->sx2 > xcross) memset (&mostbuf[xcross], 0, (wallc->sx2-xcross)*sizeof(mostbuf[0]));
		}
		else
		{ // the left side is above the screen
			if (xcross <= wallc->sx2) { iy1 = inty; ix1 = xcross; }
			if (xcross > wallc->sx1) memset (&mostbuf[wallc->sx1], 0, (xcross-wallc->sx1)*sizeof(mostbuf[0]));
		}
	}

	if (bad & 12)
	{ // the line intersects the bottom of the screen
		double t = (z-s3) / (s4-s3);
		double inty = wallc->sz1 + t * (wallc->sz2 - wallc->sz1);
		int xcross = xs_RoundToInt(wallc->sx1 + (t * wallc->sz2 * (wallc->sx2 - wallc->sx1)) / inty);

		if ((bad & 12) == 8)
		{ // the right side is below the screen
			if (wallc->sx1 <= xcross) { iy2 = inty; ix2 = xcross; }
			if (wallc->sx2 > xcross) clearbufshort (&mostbuf[xcross], wallc->sx2 - xcross, viewheight);
		}
		else
		{ // the left side is below the screen
			if (xcross <= wallc->sx2) { iy1 = inty; ix1 = xcross; }
			if (xcross > wallc->sx1) clearbufshort (&mostbuf[wallc->sx1], xcross - wallc->sx1, viewheight);
		}
	}

	y = z * InvZtoScale / iy1;
	if (ix2 == ix1)
	{
		mostbuf[ix1] = (short)xs_RoundToInt(y + CenterY);
	}
	else
	{
		fixed_t yinc = FLOAT2FIXED(((z * InvZtoScale / iy2) - y) / (ix2 - ix1));
		qinterpolatedown16short (&mostbuf[ix1], ix2-ix1, FLOAT2FIXED(y + CenterY) + FRACUNIT/2, yinc);
	}
	return bad;
}

int WallMost (short *mostbuf, const secplane_t &plane, const FWallCoords *wallc)
{
	if (!plane.isSlope())
	{
		return OWallMost(mostbuf, plane.Zat0() - ViewPos.Z, wallc);
	}

	double x, y, den, z1, z2, oz1, oz2;
	double s1, s2, s3, s4;
	int bad, ix1, ix2;
	double iy1, iy2;

	// Get Z coordinates at both ends of the line
	if (MirrorFlags & RF_XFLIP)
	{
		x = curline->v2->fX();
		y = curline->v2->fY();
		if (wallc->sx1 == 0 && 0 != (den = wallc->tleft.X - wallc->tright.X + wallc->tleft.Y - wallc->tright.Y))
		{
			double frac = (wallc->tleft.Y + wallc->tleft.X) / den;
			x -= frac * (x - curline->v1->fX());
			y -= frac * (y - curline->v1->fY());
		}
		z1 = ViewPos.Z - plane.ZatPoint(x, y);

		if (wallc->sx2 > wallc->sx1 + 1)
		{
			x = curline->v1->fX();
			y = curline->v1->fY();
			if (wallc->sx2 == viewwidth && 0 != (den = wallc->tleft.X - wallc->tright.X - wallc->tleft.Y + wallc->tright.Y))
			{
				double frac = (wallc->tright.Y - wallc->tright.X) / den;
				x += frac * (curline->v2->fX() - x);
				y += frac * (curline->v2->fY() - y);
			}
			z2 = ViewPos.Z - plane.ZatPoint(x, y);
		}
		else
		{
			z2 = z1;
		}
	}
	else
	{
		x = curline->v1->fX();
		y = curline->v1->fY();
		if (wallc->sx1 == 0 && 0 != (den = wallc->tleft.X - wallc->tright.X + wallc->tleft.Y - wallc->tright.Y))
		{
			double frac = (wallc->tleft.Y + wallc->tleft.X) / den;
			x += frac * (curline->v2->fX() - x);
			y += frac * (curline->v2->fY() - y);
		}
		z1 = ViewPos.Z - plane.ZatPoint(x, y);

		if (wallc->sx2 > wallc->sx1 + 1)
		{
			x = curline->v2->fX();
			y = curline->v2->fY();
			if (wallc->sx2 == viewwidth && 0 != (den = wallc->tleft.X - wallc->tright.X - wallc->tleft.Y + wallc->tright.Y))
			{
				double frac = (wallc->tright.Y - wallc->tright.X) / den;
				x -= frac * (x - curline->v1->fX());
				y -= frac * (y - curline->v1->fY());
			}
			z2 = ViewPos.Z - plane.ZatPoint(x, y);
		}
		else
		{
			z2 = z1;
		}
	}

	s1 = globaluclip * wallc->sz1; s2 = globaluclip * wallc->sz2;
	s3 = globaldclip * wallc->sz1; s4 = globaldclip * wallc->sz2;
	bad = (z1<s1)+((z2<s2)<<1)+((z1>s3)<<2)+((z2>s4)<<3);

	ix1 = wallc->sx1; ix2 = wallc->sx2;
	iy1 = wallc->sz1; iy2 = wallc->sz2;
	oz1 = z1; oz2 = z2;

	if ((bad&3) == 3)
	{ // The entire line is above the screen
		memset (&mostbuf[ix1], 0, (ix2-ix1)*sizeof(mostbuf[0]));
		return bad;
	}

	if ((bad&12) == 12)
	{ // The entire line is below the screen
		clearbufshort (&mostbuf[ix1], ix2-ix1, viewheight);
		return bad;

	}

	if (bad&3)
	{ // The line intersects the top of the screen
			//inty = intz / (globaluclip>>16)
		double t = (oz1-s1) / (s2-s1+oz1-oz2);
		double inty = wallc->sz1 + t * (wallc->sz2-wallc->sz1);
		double intz = oz1 + t * (oz2-oz1);
		int xcross = wallc->sx1 + xs_RoundToInt((t * wallc->sz2 * (wallc->sx2-wallc->sx1)) / inty);

		//t = divscale30((x1<<4)-xcross*yb1[w],xcross*(yb2[w]-yb1[w])-((x2-x1)<<4));
		//inty = yb1[w] + mulscale30(yb2[w]-yb1[w],t);
		//intz = z1 + mulscale30(z2-z1,t);

		if ((bad&3) == 2)
		{ // The right side of the line is above the screen
			if (wallc->sx1 <= xcross) { z2 = intz; iy2 = inty; ix2 = xcross; }
			memset (&mostbuf[xcross], 0, (wallc->sx2-xcross)*sizeof(mostbuf[0]));
		}
		else
		{ // The left side of the line is above the screen
			if (xcross <= wallc->sx2) { z1 = intz; iy1 = inty; ix1 = xcross; }
			memset (&mostbuf[wallc->sx1], 0, (xcross-wallc->sx1)*sizeof(mostbuf[0]));
		}
	}

	if (bad&12)
	{ // The line intersects the bottom of the screen
			//inty = intz / (globaldclip>>16)
		double t = (oz1-s3) / (s4-s3+oz1-oz2);
		double inty = wallc->sz1 + t * (wallc->sz2-wallc->sz1);
		double intz = oz1 + t * (oz2-oz1);
		int xcross = wallc->sx1 + xs_RoundToInt((t * wallc->sz2 * (wallc->sx2-wallc->sx1)) / inty);

		//t = divscale30((x1<<4)-xcross*yb1[w],xcross*(yb2[w]-yb1[w])-((x2-x1)<<4));
		//inty = yb1[w] + mulscale30(yb2[w]-yb1[w],t);
		//intz = z1 + mulscale30(z2-z1,t);

		if ((bad&12) == 8)
		{ // The right side of the line is below the screen
			if (wallc->sx1 <= xcross) { z2 = intz; iy2 = inty; ix2 = xcross; }
			if (wallc->sx2 > xcross) clearbufshort (&mostbuf[xcross], wallc->sx2-xcross, viewheight);
		}
		else
		{ // The left side of the line is below the screen
			if (xcross <= wallc->sx2) { z1 = intz; iy1 = inty; ix1 = xcross; }
			if (xcross > wallc->sx1) clearbufshort (&mostbuf[wallc->sx1], xcross-wallc->sx1, viewheight);
		}
	}

	y = z1 * InvZtoScale / iy1;
	if (ix2 == ix1)
	{
		mostbuf[ix1] = (short)xs_RoundToInt(y + CenterY);
	}
	else
	{
		fixed_t yinc = FLOAT2FIXED(((z2 * InvZtoScale / iy2) - y) / (ix2-ix1));
		qinterpolatedown16short (&mostbuf[ix1], ix2-ix1, FLOAT2FIXED(y + CenterY) + FRACUNIT/2, yinc);
	}

	return bad;
}

static void PrepWallRoundFix(fixed_t *lwall, fixed_t walxrepeat, int x1, int x2)
{
	// fix for rounding errors
	walxrepeat = abs(walxrepeat);
	fixed_t fix = (MirrorFlags & RF_XFLIP) ? walxrepeat-1 : 0;
	int x;

	if (x1 > 0)
	{
		for (x = x1; x < x2; x++)
		{
			if ((unsigned)lwall[x] >= (unsigned)walxrepeat)
			{
				lwall[x] = fix;
			}
			else
			{
				break;
			}
		}
	}
	fix = walxrepeat - 1 - fix;
	for (x = x2-1; x >= x1; x--)
	{
		if ((unsigned)lwall[x] >= (unsigned)walxrepeat)
		{
			lwall[x] = fix;
		}
		else
		{
			break;
		}
	}
}

void PrepWall (float *swall, fixed_t *lwall, double walxrepeat, int x1, int x2)
{ // swall = scale, lwall = texturecolumn
	double top, bot, i;
	double xrepeat = fabs(walxrepeat * 65536);
	double depth_scale = WallT.InvZstep * WallTMapScale2;
	double depth_org = -WallT.UoverZstep * WallTMapScale2;

	i = x1 - centerx;
	top = WallT.UoverZorg + WallT.UoverZstep * i;
	bot = WallT.InvZorg + WallT.InvZstep * i;

	for (int x = x1; x < x2; x++)
	{
		double frac = top / bot;
		if (walxrepeat < 0)
		{
			lwall[x] = xs_RoundToInt(xrepeat - frac * xrepeat);
		}
		else
		{
			lwall[x] = xs_RoundToInt(frac * xrepeat);
		}
		swall[x] = float(frac * depth_scale + depth_org);
		top += WallT.UoverZstep;
		bot += WallT.InvZstep;
	}
	PrepWallRoundFix(lwall, FLOAT2FIXED(walxrepeat), x1, x2);
}

void PrepLWall (fixed_t *lwall, double walxrepeat, int x1, int x2)
{ // lwall = texturecolumn
	double top, bot, i;
	double xrepeat = fabs(walxrepeat * 65536);
	double topstep, botstep;

	i = x1 - centerx;
	top = WallT.UoverZorg + WallT.UoverZstep * i;
	bot = WallT.InvZorg + WallT.InvZstep * i;

	top *= xrepeat;
	topstep = WallT.UoverZstep * xrepeat;
	botstep = WallT.InvZstep;

	for (int x = x1; x < x2; x++)
	{
		if (walxrepeat < 0)
		{
			lwall[x] = xs_RoundToInt(xrepeat - top / bot);
		}
		else
		{
			lwall[x] = xs_RoundToInt(top / bot);
		}
		top += topstep;
		bot += botstep;
	}
	PrepWallRoundFix(lwall, FLOAT2FIXED(walxrepeat), x1, x2);
}

// pass = 0: when seg is first drawn
//		= 1: drawing masked textures (including sprites)
// Currently, only pass = 0 is done or used

static void R_RenderDecal (side_t *wall, DBaseDecal *decal, drawseg_t *clipper, int pass)
{
	DVector2 decal_left, decal_right, decal_pos;
	int x1, x2;
	double yscale;
	BYTE flipx;
	double zpos;
	int needrepeat = 0;
	sector_t *front, *back;
	bool calclighting;
	bool rereadcolormap;
	FDynamicColormap *usecolormap;

	if (decal->RenderFlags & RF_INVISIBLE || !viewactive || !decal->PicNum.isValid())
		return;

	// Determine actor z
	zpos = decal->Z;
	front = curline->frontsector;
	back = (curline->backsector != NULL) ? curline->backsector : curline->frontsector;
	switch (decal->RenderFlags & RF_RELMASK)
	{
	default:
		zpos = decal->Z;
		break;
	case RF_RELUPPER:
		if (curline->linedef->flags & ML_DONTPEGTOP)
		{
			zpos = decal->Z + front->GetPlaneTexZ(sector_t::ceiling);
		}
		else
		{
			zpos = decal->Z + back->GetPlaneTexZ(sector_t::ceiling);
		}
		break;
	case RF_RELLOWER:
		if (curline->linedef->flags & ML_DONTPEGBOTTOM)
		{
			zpos = decal->Z + front->GetPlaneTexZ(sector_t::ceiling);
		}
		else
		{
			zpos = decal->Z + back->GetPlaneTexZ(sector_t::floor);
		}
		break;
	case RF_RELMID:
		if (curline->linedef->flags & ML_DONTPEGBOTTOM)
		{
			zpos = decal->Z + front->GetPlaneTexZ(sector_t::floor);
		}
		else
		{
			zpos = decal->Z + front->GetPlaneTexZ(sector_t::ceiling);
		}
	}

	WallSpriteTile = TexMan(decal->PicNum, true);
	flipx = (BYTE)(decal->RenderFlags & RF_XFLIP);

	if (WallSpriteTile == NULL || WallSpriteTile->UseType == FTexture::TEX_Null)
	{
		return;
	}

	// Determine left and right edges of sprite. Since this sprite is bound
	// to a wall, we use the wall's angle instead of the decal's. This is
	// pretty much the same as what R_AddLine() does.

	FWallCoords savecoord = WallC;

	double edge_right = WallSpriteTile->GetWidth();
	double edge_left = WallSpriteTile->LeftOffset;
	edge_right = (edge_right - edge_left) * decal->ScaleX;
	edge_left *= decal->ScaleX;

	double dcx, dcy;
	decal->GetXY(wall, dcx, dcy);
	decal_pos = { dcx, dcy };

	DVector2 angvec = (curline->v2->fPos() - curline->v1->fPos()).Unit();

	decal_left = decal_pos - edge_left * angvec - ViewPos;
	decal_right = decal_pos + edge_right * angvec - ViewPos;

	if (WallC.Init(decal_left, decal_right, TOO_CLOSE_Z))
		goto done;

	x1 = WallC.sx1;
	x2 = WallC.sx2;

	if (x1 >= clipper->x2 || x2 <= clipper->x1)
		goto done;

	WallT.InitFromWallCoords(&WallC);

	// Get the top and bottom clipping arrays
	switch (decal->RenderFlags & RF_CLIPMASK)
	{
	case RF_CLIPFULL:
		if (curline->backsector == NULL)
		{
			if (pass != 0)
			{
				goto done;
			}
			mceilingclip = walltop;
			mfloorclip = wallbottom;
		}
		else if (pass == 0)
		{
			mceilingclip = walltop;
			mfloorclip = ceilingclip;
			needrepeat = 1;
		}
		else
		{
			mceilingclip = openings + clipper->sprtopclip - clipper->x1;
			mfloorclip = openings + clipper->sprbottomclip - clipper->x1;
		}
		break;

	case RF_CLIPUPPER:
		if (pass != 0)
		{
			goto done;
		}
		mceilingclip = walltop;
		mfloorclip = ceilingclip;
		break;

	case RF_CLIPMID:
		if (curline->backsector != NULL && pass != 2)
		{
			goto done;
		}
		mceilingclip = openings + clipper->sprtopclip - clipper->x1;
		mfloorclip = openings + clipper->sprbottomclip - clipper->x1;
		break;

	case RF_CLIPLOWER:
		if (pass != 0)
		{
			goto done;
		}
		mceilingclip = floorclip;
		mfloorclip = wallbottom;
		break;
	}

	yscale = decal->ScaleY;
	dc_texturemid = WallSpriteTile->TopOffset + (zpos - ViewPos.Z) / yscale;

	// Clip sprite to drawseg
	x1 = MAX<int>(clipper->x1, x1);
	x2 = MIN<int>(clipper->x2, x2);
	if (x1 >= x2)
	{
		goto done;
	}

	PrepWall (swall, lwall, WallSpriteTile->GetWidth(), x1, x2);

	if (flipx)
	{
		int i;
		int right = (WallSpriteTile->GetWidth() << FRACBITS) - 1;

		for (i = x1; i < x2; i++)
		{
			lwall[i] = right - lwall[i];
		}
	}

	// Prepare lighting
	calclighting = false;
	usecolormap = basecolormap;
	rereadcolormap = true;

	// Decals that are added to the scene must fade to black.
	if (decal->RenderStyle == LegacyRenderStyles[STYLE_Add] && usecolormap->Fade != 0)
	{
		usecolormap = GetSpecialLights(usecolormap->Color, 0, usecolormap->Desaturate);
		rereadcolormap = false;
	}

	rw_light = rw_lightleft + (x1 - WallC.sx1) * rw_lightstep;
	if (fixedlightlev >= 0)
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : usecolormap, 0, FIXEDLIGHT2SHADE(fixedlightlev));
	else if (fixedcolormap != NULL)
		R_SetColorMapLight(fixedcolormap, 0, 0);
	else if (!foggy && (decal->RenderFlags & RF_FULLBRIGHT))
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : usecolormap, 0, 0);
	else
		calclighting = true;

	// Draw it
	if (decal->RenderFlags & RF_YFLIP)
	{
		sprflipvert = true;
		yscale = -yscale;
		dc_texturemid -= WallSpriteTile->GetHeight();
	}
	else
	{
		sprflipvert = false;
	}

	MaskedScaleY = float(1 / yscale);
	do
	{
		dc_x = x1;
		ESPSResult mode;

		mode = R_SetPatchStyle (decal->RenderStyle, (float)decal->Alpha, decal->Translation, decal->AlphaColor);

		// R_SetPatchStyle can modify basecolormap.
		if (rereadcolormap)
		{
			usecolormap = basecolormap;
		}

		if (mode == DontDraw)
		{
			needrepeat = 0;
		}
		else
		{
			int stop4;

			if (mode == DoDraw0)
			{ // 1 column at a time
				stop4 = dc_x;
			}
			else	 // DoDraw1
			{ // up to 4 columns at a time
				stop4 = x2 & ~3;
			}

			while ((dc_x < stop4) && (dc_x & 3))
			{
				if (calclighting)
				{ // calculate lighting
					R_SetColorMapLight(usecolormap, rw_light, wallshade);
				}
				R_WallSpriteColumn (false);
				dc_x++;
			}

			while (dc_x < stop4)
			{
				if (calclighting)
				{ // calculate lighting
					R_SetColorMapLight(usecolormap, rw_light, wallshade);
				}
				rt_initcols(nullptr);
				for (int zz = 4; zz; --zz)
				{
					R_WallSpriteColumn (true);
					dc_x++;
				}
				rt_draw4cols (dc_x - 4);
			}

			while (dc_x < x2)
			{
				if (calclighting)
				{ // calculate lighting
					R_SetColorMapLight(usecolormap, rw_light, wallshade);
				}
				R_WallSpriteColumn (false);
				dc_x++;
			}
		}

		// If this sprite is RF_CLIPFULL on a two-sided line, needrepeat will
		// be set 1 if we need to draw on the lower wall. In all other cases,
		// needrepeat will be 0, and the while will fail.
		mceilingclip = floorclip;
		mfloorclip = wallbottom;
		R_FinishSetPatchStyle ();
	} while (needrepeat--);

	colfunc = basecolfunc;
	hcolfunc_post1 = rt_map1col;
	hcolfunc_post4 = rt_map4cols;

	R_FinishSetPatchStyle ();
done:
	WallC = savecoord;
}
