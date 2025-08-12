/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_act0.cpp NewGRF Action 0x00 handler. */

#include "../stdafx.h"
#include "../debug.h"
#include "../newgrf_engine.h"
#include "../newgrf_extension.h"
#include "../newgrf_badge.h"
#include "../newgrf_badge_type.h"
#include "../newgrf_cargo.h"
#include "../timer/timer_game_calendar.h"
#include "../error.h"
#include "../vehicle_base.h"
#include "newgrf_bytereader.h"
#include "newgrf_internal.h"

#include "../table/strings.h"

#include "../safeguards.h"

ChangeInfoResult HandleAction0PropertyDefault(ByteReader &buf, int prop)
{
	if (prop == A0RPI_UNKNOWN_ERROR) {
		return CIR_DISABLED;
	} else if (prop < A0RPI_UNKNOWN_IGNORE) {
		return CIR_UNKNOWN;
	} else {
		buf.Skip(buf.ReadExtendedByte());
		return CIR_SUCCESS;
	}
}

bool MappedPropertyLengthMismatch(ByteReader &buf, uint expected_size, const GRFFilePropertyRemapEntry *mapping_entry)
{
	uint length = buf.ReadExtendedByte();
	if (length != expected_size) {
		if (mapping_entry != nullptr) {
			GrfMsg(2, "Ignoring use of mapped property: {}, feature: {}, mapped to: {:X}{}, with incorrect data size: {} instead of {}",
					mapping_entry->name, GetFeatureString(mapping_entry->feature),
					mapping_entry->property_id, mapping_entry->extended ? " (extended)" : "",
					length, expected_size);
		}
		buf.Skip(length);
		return true;
	} else {
		return false;
	}
}

struct GRFFilePropertyDescriptor {
	int prop;
	const GRFFilePropertyRemapEntry *entry;

	GRFFilePropertyDescriptor(int prop, const GRFFilePropertyRemapEntry *entry)
			: prop(prop), entry(entry) {}
};

static GRFFilePropertyDescriptor ReadAction0PropertyID(ByteReader &buf, uint8_t feature)
{
	uint8_t raw_prop = buf.ReadByte();
	const GRFFilePropertyRemapSet &remap = _cur.grffile->action0_property_remaps[feature];
	if (remap.remapped_ids[raw_prop]) {
		auto iter = remap.mapping.find(raw_prop);
		assert(iter != remap.mapping.end());
		const GRFFilePropertyRemapEntry &def = iter->second;
		int prop = def.id;
		if (prop == A0RPI_UNKNOWN_ERROR) {
			GrfMsg(0, "Error: Unimplemented mapped property: {}, feature: {}, mapped to: {:X}", def.name, GetFeatureString(def.feature), raw_prop);
			GRFError *error = DisableGrf(STR_NEWGRF_ERROR_UNIMPLEMETED_MAPPED_PROPERTY);
			error->data = stredup(def.name);
			error->param_value[1] = def.feature;
			error->param_value[2] = raw_prop;
		} else if (prop == A0RPI_UNKNOWN_IGNORE) {
			GrfMsg(2, "Ignoring unimplemented mapped property: {}, feature: {}, mapped to: {:X}", def.name, GetFeatureString(def.feature), raw_prop);
		} else if (prop == A0RPI_ID_EXTENSION) {
			const uint8_t *outer_data = buf.Data();
			size_t outer_length = buf.ReadExtendedByte();
			uint16_t mapped_id = buf.ReadWord();
			const uint8_t *inner_data = buf.Data();
			size_t inner_length = buf.ReadExtendedByte();
			if (inner_length + (inner_data - outer_data) != outer_length) {
				GrfMsg(2, "Ignoring extended ID property with malformed lengths: {}, feature: {}, mapped to: {:X}", def.name, GetFeatureString(def.feature), raw_prop);
				buf.ResetReadPosition(outer_data);
				return GRFFilePropertyDescriptor(A0RPI_UNKNOWN_IGNORE, &def);
			}

			auto ext = _cur.grffile->action0_extended_property_remaps.find((((uint32_t)feature) << 16) | mapped_id);
			if (ext != _cur.grffile->action0_extended_property_remaps.end()) {
				buf.ResetReadPosition(inner_data);
				const GRFFilePropertyRemapEntry &ext_def = ext->second;
				prop = ext_def.id;
				if (prop == A0RPI_UNKNOWN_ERROR) {
					GrfMsg(0, "Error: Unimplemented mapped extended ID property: {}, feature: {}, mapped to: {:X} (via {:X})", ext_def.name, GetFeatureString(ext_def.feature), mapped_id, raw_prop);
					GRFError *error = DisableGrf(STR_NEWGRF_ERROR_UNIMPLEMETED_MAPPED_PROPERTY);
					error->data = stredup(ext_def.name);
					error->param_value[1] = ext_def.feature;
					error->param_value[2] = 0xE0000 | mapped_id;
				} else if (prop == A0RPI_UNKNOWN_IGNORE) {
					GrfMsg(2, "Ignoring unimplemented mapped extended ID property: {}, feature: {}, mapped to: {:X} (via {:X})", ext_def.name, GetFeatureString(ext_def.feature), mapped_id, raw_prop);
				}
				return GRFFilePropertyDescriptor(prop, &ext_def);
			} else {
				GrfMsg(2, "Ignoring unknown extended ID property: {}, feature: {}, mapped to: {:X} (via {:X})", def.name, GetFeatureString(def.feature), mapped_id, raw_prop);
				buf.ResetReadPosition(outer_data);
				return GRFFilePropertyDescriptor(A0RPI_UNKNOWN_IGNORE, &def);
			}
		}
		return GRFFilePropertyDescriptor(prop, &def);
	} else {
		return GRFFilePropertyDescriptor(raw_prop, nullptr);
	}
}

/**
 * Define properties common to all vehicles
 * @param ei Engine info.
 * @param prop The property to change.
 * @param buf The property value.
 * @return ChangeInfoResult.
 */
ChangeInfoResult CommonVehicleChangeInfo(EngineInfo *ei, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf)
{
	switch (prop) {
		case 0x00: // Introduction date
			ei->base_intro = CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR + buf.ReadWord();
			break;

		case 0x02: // Decay speed
			ei->decay_speed = buf.ReadByte();
			break;

		case 0x03: // Vehicle life
			ei->lifelength = CalTime::YearDelta{buf.ReadByte()};
			break;

		case 0x04: // Model life
			ei->base_life = CalTime::YearDelta{buf.ReadByte()};
			break;

		case 0x06: // Climates available
			ei->climates = LandscapeTypes{buf.ReadByte()};
			break;

		case PROP_VEHICLE_LOAD_AMOUNT: // 0x07 Loading speed
			/* Amount of cargo loaded during a vehicle's "loading tick" */
			ei->load_amount = buf.ReadByte();
			break;

		default:
			return HandleAction0PropertyDefault(buf, prop);
	}

	return CIR_SUCCESS;
}

/**
 * Skip a list of badges.
 * @param buf Buffer reader containing list of badges to skip.
 */
void SkipBadgeList(ByteReader &buf)
{
	uint16_t count = buf.ReadWord();
	while (count-- > 0) {
		buf.ReadWord();
	}
}

/**
 * Read a list of badges.
 * @param buf Buffer reader containing list of badges to read.
 * @param feature The feature of the badge list.
 * @returns list of badges.
 */
std::vector<BadgeID> ReadBadgeList(ByteReader &buf, GrfSpecFeature feature)
{
	uint16_t count = buf.ReadWord();

	std::vector<BadgeID> badges;
	badges.reserve(count);

	while (count-- > 0) {
		uint16_t local_index = buf.ReadWord();
		if (local_index >= std::size(_cur.grffile->badge_list)) {
			GrfMsg(1, "ReadBadgeList: Badge label {} out of range (max {}), skipping.", local_index, std::size(_cur.grffile->badge_list) - 1);
			continue;
		}

		BadgeID index = _cur.grffile->badge_list[local_index];

		/* Is badge already present? */
		if (std::ranges::find(badges, index) != std::end(badges)) continue;

		badges.push_back(index);
		MarkBadgeSeen(index, feature);
	}

	return badges;
}

bool HandleChangeInfoResult(const char *caller, ChangeInfoResult cir, GrfSpecFeature feature, int property)
{
	switch (cir) {
		default: NOT_REACHED();

		case CIR_DISABLED:
			/* Error has already been printed; just stop parsing */
			return true;

		case CIR_SUCCESS:
			return false;

		case CIR_UNHANDLED:
			GrfMsg(1, "{}: Ignoring property 0x{:02X} of feature {} (not implemented)", caller, property, GetFeatureString(feature));
			return false;

		case CIR_UNKNOWN:
			GrfMsg(0, "{}: Unknown property 0x{:02X} of feature {}, disabling", caller, property, GetFeatureString(feature));
			[[fallthrough]];

		case CIR_INVALID_ID: {
			/* No debug message for an invalid ID, as it has already been output */
			GRFError *error = DisableGrf(cir == CIR_INVALID_ID ? STR_NEWGRF_ERROR_INVALID_ID : STR_NEWGRF_ERROR_UNKNOWN_PROPERTY);
			if (cir != CIR_INVALID_ID) error->param_value[1] = property;
			return true;
		}
	}
}

/** Helper class to invoke a GrfChangeInfoHandler. */
struct InvokeGrfChangeInfoHandler {
	template <GrfSpecFeature TFeature>
	static ChangeInfoResult Invoke(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf, GrfLoadingStage stage)
	{
		switch (stage) {
			case GLS_RESERVE: return GrfChangeInfoHandler<TFeature>::Reserve(first, last, prop, mapping_entry, buf);
			case GLS_ACTIVATION: return GrfChangeInfoHandler<TFeature>::Activation(first, last, prop, mapping_entry, buf);
			default: NOT_REACHED();
		}
	}

	using Invoker = ChangeInfoResult(*)(uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf, GrfLoadingStage stage);
	static constexpr Invoker funcs[] { // Must be listed in feature order.
		Invoke<GSF_TRAINS>,    Invoke<GSF_ROADVEHICLES>,  Invoke<GSF_SHIPS>,         Invoke<GSF_AIRCRAFT>,
		Invoke<GSF_STATIONS>,  Invoke<GSF_CANALS>,        Invoke<GSF_BRIDGES>,       Invoke<GSF_HOUSES>,
		Invoke<GSF_GLOBALVAR>, Invoke<GSF_INDUSTRYTILES>, Invoke<GSF_INDUSTRIES>,    Invoke<GSF_CARGOES>,
		Invoke<GSF_SOUNDFX>,   Invoke<GSF_AIRPORTS>,      Invoke<GSF_SIGNALS>,       Invoke<GSF_OBJECTS>,
		Invoke<GSF_RAILTYPES>, Invoke<GSF_AIRPORTTILES>,  Invoke<GSF_ROADTYPES>,     Invoke<GSF_TRAMTYPES>,
		Invoke<GSF_ROADSTOPS>, Invoke<GSF_BADGES>,        Invoke<GSF_NEWLANDSCAPE>,  nullptr /* GSF_FAKE_TOWNS */
	};

	static ChangeInfoResult Invoke(GrfSpecFeature feature, uint first, uint last, int prop, const GRFFilePropertyRemapEntry *mapping_entry, ByteReader &buf, GrfLoadingStage stage)
	{
		Invoker func = feature < std::size(funcs) ? funcs[feature] : nullptr;
		if (func == nullptr) return CIR_UNKNOWN;
		return func(first, last, prop, mapping_entry, buf, stage);
	}
};

/* Action 0x00 */
static void FeatureChangeInfo(ByteReader &buf)
{
	/* <00> <feature> <num-props> <num-info> <id> (<property <new-info>)...
	 *
	 * B feature
	 * B num-props     how many properties to change per vehicle/station
	 * B num-info      how many vehicles/stations to change
	 * E id            ID of first vehicle/station to change, if num-info is
	 *                 greater than one, this one and the following
	 *                 vehicles/stations will be changed
	 * B property      what property to change, depends on the feature
	 * V new-info      new bytes of info (variable size; depends on properties) */

	GrfSpecFeatureRef feature_ref = ReadFeature(buf.ReadByte());
	GrfSpecFeature feature = feature_ref.id;
	uint8_t numprops = buf.ReadByte();
	uint numinfo  = buf.ReadByte();
	uint engine   = buf.ReadExtendedByte();

	if (feature >= GSF_END) {
		GrfMsg(1, "FeatureChangeInfo: Unsupported feature {} skipping", GetFeatureString(feature_ref));
		return;
	}

	GrfMsg(6, "FeatureChangeInfo: Feature {}, {} properties, to apply to {}+{}",
	               GetFeatureString(feature_ref), numprops, engine, numinfo);

	/* Test if feature handles change. */
	ChangeInfoResult cir_test = InvokeGrfChangeInfoHandler::Invoke(feature, 0, 0, 0, nullptr, buf, GLS_ACTIVATION);
	if (cir_test == CIR_UNHANDLED) return;
	if (cir_test == CIR_UNKNOWN) {
		GrfMsg(1, "FeatureChangeInfo: Unsupported feature {}, skipping", GetFeatureString(feature_ref));
		return;
	}

	/* Mark the feature as used by the grf */
	SetBit(_cur.grffile->grf_features, feature);

	while (numprops-- && buf.HasData()) {
		GRFFilePropertyDescriptor desc = ReadAction0PropertyID(buf, feature);

		ChangeInfoResult cir = InvokeGrfChangeInfoHandler::Invoke(feature, engine, engine + numinfo, desc.prop, desc.entry, buf, GLS_ACTIVATION);
		if (HandleChangeInfoResult("FeatureChangeInfo", cir, feature, desc.prop)) return;
	}
}

/* Action 0x00 (GLS_SAFETYSCAN) */
static void SafeChangeInfo(ByteReader &buf)
{
	GrfSpecFeatureRef feature = ReadFeature(buf.ReadByte());
	uint8_t numprops = buf.ReadByte();
	uint numinfo = buf.ReadByte();
	buf.ReadExtendedByte(); // id

	if (feature.id == GSF_BRIDGES && numprops == 1) {
		GRFFilePropertyDescriptor desc = ReadAction0PropertyID(buf, feature.id);

		/* Bridge property 0x0D is redefinition of sprite layout tables, which
		 * is considered safe. */
		if (desc.prop == 0x0D) return;
	} else if (feature.id == GSF_GLOBALVAR && numprops == 1) {
		GRFFilePropertyDescriptor desc = ReadAction0PropertyID(buf, feature.id);
		/* Engine ID Mappings are safe, if the source is static */
		if (desc.prop == 0x11) {
			bool is_safe = true;
			for (uint i = 0; i < numinfo; i++) {
				uint32_t s = buf.ReadDWord();
				buf.ReadDWord(); // dest
				const GRFConfig *grfconfig = GetGRFConfig(s);
				if (grfconfig != nullptr && !grfconfig->flags.Test(GRFConfigFlag::Static)) {
					is_safe = false;
					break;
				}
			}
			if (is_safe) return;
		}
	}

	GRFUnsafe(buf);
}

/* Action 0x00 (GLS_RESERVE) */
static void ReserveChangeInfo(ByteReader &buf)
{
	GrfSpecFeatureRef feature_ref = ReadFeature(buf.ReadByte());
	GrfSpecFeature feature = feature_ref.id;

	/* Test if feature handles reservation. */
	ChangeInfoResult cir_test = InvokeGrfChangeInfoHandler::Invoke(feature, 0, 0, 0, nullptr, buf, GLS_RESERVE);
	if (cir_test == CIR_UNHANDLED) return;
	if (cir_test == CIR_UNKNOWN) {
		GrfMsg(1, "ReserveChangeInfo: Unsupported feature {}, skipping", GetFeatureString(feature_ref));
		return;
	}

	uint8_t numprops = buf.ReadByte();
	uint8_t numinfo = buf.ReadByte();
	uint16_t index = buf.ReadExtendedByte();

	while (numprops-- && buf.HasData()) {
		GRFFilePropertyDescriptor desc = ReadAction0PropertyID(buf, feature);

		ChangeInfoResult cir = InvokeGrfChangeInfoHandler::Invoke(feature, index, index + numinfo, desc.prop, desc.entry, buf, GLS_RESERVE);
		if (HandleChangeInfoResult("ReserveChangeInfo", cir, feature, desc.prop)) return;
	}
}

template <> void GrfActionHandler<0x00>::FileScan(ByteReader &) { }
template <> void GrfActionHandler<0x00>::SafetyScan(ByteReader &buf) { SafeChangeInfo(buf); }
template <> void GrfActionHandler<0x00>::LabelScan(ByteReader &) { }
template <> void GrfActionHandler<0x00>::Init(ByteReader &) { }
template <> void GrfActionHandler<0x00>::Reserve(ByteReader &buf) { ReserveChangeInfo(buf); }
template <> void GrfActionHandler<0x00>::Activation(ByteReader &buf) { FeatureChangeInfo(buf); }
