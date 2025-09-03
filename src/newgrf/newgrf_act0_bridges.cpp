/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_act0_bidges.cpp NewGRF Action 0x00 handler for bridges. */

#include "../stdafx.h"
#include "../debug.h"
#include "../bridge.h"
#include "../newgrf_extension.h"
#include "newgrf_bytereader.h"
#include "newgrf_internal.h"
#include "newgrf_stringmapping.h"

#include "../safeguards.h"

/**
 * Define properties for bridges
 * @param first Local ID of the first bridge.
 * @param last Local ID of the last bridge.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult BridgeChangeInfo(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf)
{
	ChangeInfoResult ret = CIR_SUCCESS;

	if (last > MAX_BRIDGES) {
		GrfMsg(1, "BridgeChangeInfo: Bridge {} is invalid, max {}, ignoring", last, MAX_BRIDGES);
		return CIR_INVALID_ID;
	}

	for (uint id = first; id < last; ++id) {
		BridgeSpec *bridge = &_bridge[id];

		switch (prop) {
			case 0x08: { // Year of availability
				/* We treat '0' as always available */
				uint8_t year = buf.ReadByte();
				bridge->avail_year = (year > 0 ? CalTime::ORIGINAL_BASE_YEAR + year : CalTime::Year{0});
				break;
			}

			case 0x09: // Minimum length
				bridge->min_length = buf.ReadByte();
				break;

			case 0x0A: // Maximum length
				bridge->max_length = buf.ReadByte();
				if (bridge->max_length > 16) bridge->max_length = UINT16_MAX;
				break;

			case 0x0B: // Cost factor
				bridge->price = buf.ReadByte();
				break;

			case 0x0C: // Maximum speed
				bridge->speed = buf.ReadWord();
				if (bridge->speed == 0) bridge->speed = UINT16_MAX;
				break;

			case 0x0D: { // Bridge sprite tables
				uint8_t tableid = buf.ReadByte();
				uint8_t numtables = buf.ReadByte();
				size_t size = tableid + numtables;

				if (bridge->sprite_table.size() < size) {
					/* Allocate memory for sprite table pointers and zero out */
					bridge->sprite_table.resize(std::min<size_t>(size, NUM_BRIDGE_PIECES));
				}

				for (; numtables-- != 0; tableid++) {
					if (tableid >= NUM_BRIDGE_PIECES) { // skip invalid data
						GrfMsg(1, "BridgeChangeInfo: Table {} >= {}, skipping", tableid, NUM_BRIDGE_PIECES);
						for (uint8_t sprite = 0; sprite < SPRITES_PER_BRIDGE_PIECE; sprite++) buf.ReadDWord();
						continue;
					}

					if (bridge->sprite_table[tableid].empty()) {
						bridge->sprite_table[tableid].resize(SPRITES_PER_BRIDGE_PIECE);
					}

					for (uint8_t sprite = 0; sprite < SPRITES_PER_BRIDGE_PIECE; sprite++) {
						SpriteID image = buf.ReadWord();
						PaletteID pal  = buf.ReadWord();

						bridge->sprite_table[tableid][sprite].sprite = image;
						bridge->sprite_table[tableid][sprite].pal    = pal;

						MapSpriteMappingRecolour(&bridge->sprite_table[tableid][sprite]);
					}
				}
				if (!HasBit(bridge->ctrl_flags, BSCF_CUSTOM_PILLAR_FLAGS)) SetBit(bridge->ctrl_flags, BSCF_INVALID_PILLAR_FLAGS);
				break;
			}

			case 0x0E: // Flags; bit 0 - disable far pillars
				bridge->flags = buf.ReadByte();
				break;

			case 0x0F: // Long format year of availability (year since year 0)
				bridge->avail_year = CalTime::DeserialiseYearClamped(static_cast<int32_t>(buf.ReadDWord()));
				break;

			case 0x10: // purchase string
				AddStringForMapping(GRFStringID{buf.ReadWord()}, &bridge->material);
				break;

			case 0x11: // description of bridge with rails
				AddStringForMapping(GRFStringID{buf.ReadWord()}, &bridge->transport_name[0]);
				break;

			case 0x12: // description of bridge with roads
				AddStringForMapping(GRFStringID{buf.ReadWord()}, &bridge->transport_name[1]);
				break;

			case 0x13: // 16 bits cost multiplier
				bridge->price = buf.ReadWord();
				break;

			case A0RPI_BRIDGE_MENU_ICON:
				if (MappedPropertyLengthMismatch(buf, 4, mapping_entry)) break;
				[[fallthrough]];
			case 0x14: // purchase sprite
				bridge->sprite = buf.ReadWord();
				bridge->pal    = buf.ReadWord();
				break;

			case A0RPI_BRIDGE_PILLAR_FLAGS:
				if (MappedPropertyLengthMismatch(buf, 12, mapping_entry)) break;
				for (uint i = 0; i < 12; i++) {
					bridge->pillar_flags[i] = buf.ReadByte();
				}
				ClrBit(bridge->ctrl_flags, BSCF_INVALID_PILLAR_FLAGS);
				SetBit(bridge->ctrl_flags, BSCF_CUSTOM_PILLAR_FLAGS);
				break;

			case 0x15: { // Pillar information for each bridge piece.
				uint16_t tiles = buf.ReadExtendedByte();
				for (uint j = 0; j != tiles; ++j) {
					if (j < 6) {
						bridge->pillar_flags[j * 2] = buf.ReadByte();
						bridge->pillar_flags[(j * 2) + 1] = buf.ReadByte();
					} else {
						buf.ReadWord();
					}
				}
				ClrBit(bridge->ctrl_flags, BSCF_INVALID_PILLAR_FLAGS);
				SetBit(bridge->ctrl_flags, BSCF_CUSTOM_PILLAR_FLAGS);
				break;
			}

			case A0RPI_BRIDGE_AVAILABILITY_FLAGS: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t flags = buf.ReadByte();
				AssignBit(bridge->ctrl_flags, BSCF_NOT_AVAILABLE_TOWN, HasBit(flags, 0));
				AssignBit(bridge->ctrl_flags, BSCF_NOT_AVAILABLE_AI_GS, HasBit(flags, 1));
				break;
			}

			default:
				ret = HandleAction0PropertyDefault(buf, prop);
				break;
		}
	}

	return ret;
}

template <> ChangeInfoResult GrfChangeInfoHandler<GSF_BRIDGES>::Reserve(uint, uint, int, const GRFFilePropertyRemapEntry *, ByteReader &) { return CIR_UNHANDLED; }
template <> ChangeInfoResult GrfChangeInfoHandler<GSF_BRIDGES>::Activation(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf) { return BridgeChangeInfo(first, last, prop, mapping_entry, buf); }
