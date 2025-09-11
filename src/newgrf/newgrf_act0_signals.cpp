/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_act0_signals.cpp NewGRF Action 0x00 handler for signals. */

#include "../stdafx.h"
#include "../newgrf_extension.h"
#include "../newgrf_newsignals.h"
#include "newgrf_bytereader.h"
#include "newgrf_internal.h"
#include "newgrf_stringmapping.h"

#include "../safeguards.h"

/**
 * Define properties for signals
 * @param first Local ID (unused) first.
 * @param last Local ID (unused) last.
 * @param numinfo Number of subsequent IDs to change the property for.
 * @param prop The property to change.
 * @param mapping_entry Variable mapping entry.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
static ChangeInfoResult SignalsChangeInfo(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf)
{
	/* Properties which are handled per item */
	ChangeInfoResult ret = CIR_SUCCESS;
	for (uint id = first; id < last; ++id) {
		switch (prop) {
			case A0RPI_SIGNALS_ENABLE_PROGRAMMABLE_SIGNALS:
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				AssignBit(_cur_gps.grffile->new_signal_ctrl_flags, NSCF_PROGSIG, buf.ReadByte() != 0);
				break;

			case A0RPI_SIGNALS_ENABLE_NO_ENTRY_SIGNALS:
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				AssignBit(_cur_gps.grffile->new_signal_ctrl_flags, NSCF_NOENTRYSIG, buf.ReadByte() != 0);
				break;

			case A0RPI_SIGNALS_ENABLE_RESTRICTED_SIGNALS:
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				AssignBit(_cur_gps.grffile->new_signal_ctrl_flags, NSCF_RESTRICTEDSIG, buf.ReadByte() != 0);
				break;

			case A0RPI_SIGNALS_ENABLE_SIGNAL_RECOLOUR:
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				AssignBit(_cur_gps.grffile->new_signal_ctrl_flags, NSCF_RECOLOUR_ENABLED, buf.ReadByte() != 0);
				break;

			case A0RPI_SIGNALS_EXTRA_ASPECTS:
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				_cur_gps.grffile->new_signal_extra_aspects = std::min<uint8_t>(buf.ReadByte(), NEW_SIGNALS_MAX_EXTRA_ASPECT);
				break;

			case A0RPI_SIGNALS_NO_DEFAULT_STYLE:
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				AssignBit(_cur_gps.grffile->new_signal_style_mask, 0, buf.ReadByte() == 0);
				break;

			case A0RPI_SIGNALS_DEFINE_STYLE: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t local_id = buf.ReadByte();
				if (_num_new_signal_styles < MAX_NEW_SIGNAL_STYLES) {
					NewSignalStyle &style = _new_signal_styles[_num_new_signal_styles];
					style = {};
					_num_new_signal_styles++;
					SetBit(_cur_gps.grffile->new_signal_style_mask, _num_new_signal_styles);
					style.grf_local_id = local_id;
					style.grffile = _cur_gps.grffile;
					_cur_gps.grffile->current_new_signal_style = &style;
				} else {
					_cur_gps.grffile->current_new_signal_style = nullptr;
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_NAME: {
				if (MappedPropertyLengthMismatch(buf, 2, mapping_entry)) break;
				GRFStringID str = GRFStringID{buf.ReadWord()};
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AddStringForMapping(str, &(_cur_gps.grffile->current_new_signal_style->name));
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_NO_ASPECT_INCREASE: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AssignBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_NO_ASPECT_INC, value != 0);
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_ALWAYS_RESERVE_THROUGH: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AssignBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_ALWAYS_RESERVE_THROUGH, value != 0);
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_LOOKAHEAD_EXTRA_ASPECTS: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					SetBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_LOOKAHEAD_ASPECTS_SET);
					_cur_gps.grffile->current_new_signal_style->lookahead_extra_aspects = value;
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_LOOKAHEAD_SINGLE_SIGNAL_ONLY: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AssignBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_LOOKAHEAD_SINGLE_SIGNAL, value != 0);
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_SEMAPHORE_ENABLED: {
				if (MappedPropertyLengthMismatch(buf, 4, mapping_entry)) break;
				uint32_t mask = buf.ReadDWord();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					_cur_gps.grffile->current_new_signal_style->semaphore_mask = (uint8_t)mask;
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_ELECTRIC_ENABLED: {
				if (MappedPropertyLengthMismatch(buf, 4, mapping_entry)) break;
				uint32_t mask = buf.ReadDWord();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					_cur_gps.grffile->current_new_signal_style->electric_mask = (uint8_t)mask;
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_OPPOSITE_SIDE: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AssignBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_OPPOSITE_SIDE, value != 0);
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_COMBINED_NORMAL_SHUNT: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AssignBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_COMBINED_NORMAL_SHUNT, value != 0);
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_REALISTIC_BRAKING_ONLY: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AssignBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_REALISTIC_BRAKING_ONLY, value != 0);
				}
				break;
			}

			case A0RPI_SIGNALS_STYLE_BOTH_SIDES: {
				if (MappedPropertyLengthMismatch(buf, 1, mapping_entry)) break;
				uint8_t value = buf.ReadByte();
				if (_cur_gps.grffile->current_new_signal_style != nullptr) {
					AssignBit(_cur_gps.grffile->current_new_signal_style->style_flags, NSSF_BOTH_SIDES, value != 0);
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

template <> ChangeInfoResult GrfChangeInfoHandler<GSF_SIGNALS>::Reserve(uint, uint, int, const GRFFilePropertyRemapEntry *, ByteReader &) { return CIR_UNHANDLED; }
template <> ChangeInfoResult GrfChangeInfoHandler<GSF_SIGNALS>::Activation(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf) { return SignalsChangeInfo(first, last, prop, mapping_entry, buf); }
