/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_act0_newlandscape.cpp NewGRF Action 0x00 handler for new landscape. */

#include "../stdafx.h"
#include "../newgrf_extension.h"
#include "newgrf_bytereader.h"
#include "newgrf_internal.h"

#include "../safeguards.h"

/**
 * Define properties for new landscape
 * @param first Local ID of the first landscape.
 * @param last Local ID of the first landscape.
 * @param prop The property to change.
 * @param mapping_entry Variable mapping entry.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult NewLandscapeChangeInfo(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf)
{
	/* Properties which are handled per item */
	ChangeInfoResult ret = CIR_SUCCESS;
	for (uint id = first; id < last; ++id) {
		switch (prop) {
			case A0RPI_NEWLANDSCAPE_ENABLE_RECOLOUR: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				bool enabled = (buf.ReadByte() != 0 ? 1 : 0);
				if (id == NLA3ID_CUSTOM_ROCKS) {
					SB(_cur_gps.grffile->new_landscape_ctrl_flags, NLCF_ROCKS_RECOLOUR_ENABLED, 1, enabled);
				}
				break;
			}

			case A0RPI_NEWLANDSCAPE_ENABLE_DRAW_SNOWY_ROCKS: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				bool enabled = (buf.ReadByte() != 0 ? 1 : 0);
				if (id == NLA3ID_CUSTOM_ROCKS) {
					SB(_cur_gps.grffile->new_landscape_ctrl_flags, NLCF_ROCKS_DRAW_SNOWY_ENABLED, 1, enabled);
				}
				break;
			}

			default:
				ret = HandleAction0PropertyDefault(buf, prop);
				break;
		}
	}

	return ret;
}

template <> ChangeInfoResult GrfChangeInfoHandler<GSF_NEWLANDSCAPE>::Reserve(uint, uint, int, const GRFFilePropertyRemapEntry *, ByteReader &) { return CIR_UNHANDLED; }
template <> ChangeInfoResult GrfChangeInfoHandler<GSF_NEWLANDSCAPE>::Activation(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf) { return NewLandscapeChangeInfo(first, last, prop, mapping_entry, buf); }
