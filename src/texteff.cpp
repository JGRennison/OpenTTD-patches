/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file texteff.cpp Handling of text effects. */

#include "stdafx.h"
#include "texteff.hpp"
#include "transparency.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "settings_type.h"
#include "guitimer_func.h"
#include "zoom_func.h"
#include "window_gui.h"

#include "safeguards.h"

/** Container for all information about a text effect */
struct TextEffect : public ViewportSign {
	uint64_t params_1;   ///< DParam parameter
	uint64_t params_2;   ///< second DParam parameter
	StringID string_id;  ///< String to draw for the text effect, if INVALID_STRING_ID then it's not valid
	uint8_t duration;    ///< How long the text effect should stay, in ticks (applies only when mode == TE_RISING)
	TextEffectMode mode; ///< Type of text effect

	void Reset();
};

static std::vector<TextEffect> _text_effects; ///< Text effects are stored there
static TextEffectID _free_text_effect = 0;

/** Reset the text effect */
void TextEffect::Reset()
{
	this->MarkDirty(ZOOM_LVL_OUT_8X);
	this->width_normal = 0;
	this->string_id = INVALID_STRING_ID;
	this->params_1 = _free_text_effect;
	_free_text_effect = this - _text_effects.data();
}

/* Text Effects */
TextEffectID AddTextEffect(StringID msg, int center, int y, uint8_t duration, TextEffectMode mode, uint64_t param1, uint64_t param2)
{
	if (_game_mode == GM_MENU) return INVALID_TE_ID;

	TextEffectID i = _free_text_effect;
	if (i == _text_effects.size()) {
		_text_effects.emplace_back();
		_free_text_effect++;
	} else {
		_free_text_effect = _text_effects[i].params_1;
	}

	TextEffect &te = _text_effects[i];

	/* Start defining this object */
	te.string_id = msg;
	te.duration = duration;
	te.params_1 = param1;
	te.params_2 = param2;
	te.mode = mode;

	/* Make sure we only dirty the new area */
	te.width_normal = 0;
	SetDParam(0, param1);
	SetDParam(1, param2);
	te.UpdatePosition(ZOOM_LVL_OUT_8X, center, y, msg);

	return i;
}

void UpdateTextEffect(TextEffectID te_id, StringID msg, uint64_t param1, uint64_t param2)
{
	/* Update details */
	TextEffect *te = _text_effects.data() + te_id;
	if (msg == te->string_id && param1 == te->params_1) return;
	te->string_id = msg;
	te->params_1 = param1;
	te->params_2 = param2;

	SetDParam(0, param1);
	SetDParam(1, param2);
	te->UpdatePosition(ZOOM_LVL_OUT_8X, te->center, te->top, te->string_id, te->string_id - 1);
}

void UpdateAllTextEffectVirtCoords()
{
	for (auto &te : _text_effects) {
		if (te.string_id == INVALID_STRING_ID) continue;
		SetDParam(0, te.params_1);
		SetDParam(1, te.params_2);
		te.UpdatePosition(ZOOM_LVL_OUT_8X, te.center, te.top, te.string_id, te.string_id - 1);
	}
}

void RemoveTextEffect(TextEffectID te_id)
{
	_text_effects[te_id].Reset();
}

void MoveAllTextEffects(uint delta_ms)
{
	static GUITimer texteffecttimer = GUITimer(MILLISECONDS_PER_TICK);
	uint count = texteffecttimer.CountElapsed(delta_ms);
	if (count == 0) return;

	for (TextEffect &te : _text_effects) {
		if (te.string_id == INVALID_STRING_ID) continue;
		if (te.mode != TE_RISING) continue;

		if (te.duration < count) {
			te.Reset();
			continue;
		}

		te.MarkDirty(ZOOM_LVL_OUT_8X);
		te.duration -= count;
		te.top -= count * ZOOM_LVL_BASE;
		te.MarkDirty(ZOOM_LVL_OUT_8X);
	}
}

void InitTextEffects()
{
	_text_effects.clear();
	_text_effects.shrink_to_fit();
	_free_text_effect = 0;
}

void DrawTextEffects(ViewportDrawerDynamic *vdd, DrawPixelInfo *dpi, bool load_transparent)
{
	/* Don't draw the text effects when zoomed out a lot */
	if (dpi->zoom > ZOOM_LVL_OUT_8X) return;

	const int bottom_threshold = dpi->top + dpi->height;
	const int top_threshold = dpi->top - ScaleByZoom(WidgetDimensions::scaled.framerect.Horizontal() + GetCharacterHeight(FS_NORMAL), dpi->zoom);
	const bool show_loading = (_settings_client.gui.loading_indicators && !load_transparent);

	for (TextEffect &te : _text_effects) {
		if (te.string_id == INVALID_STRING_ID) continue;
		if ((te.mode == TE_RISING || show_loading) && te.top > top_threshold && te.top < bottom_threshold) {
			ViewportAddString(vdd, dpi, ZOOM_LVL_OUT_8X, &te, te.string_id, te.string_id - 1, STR_NULL, te.params_1, te.params_2);
		}
	}
}
