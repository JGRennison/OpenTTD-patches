/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_act14.cpp NewGRF Action 0x14 handler. */

#include "../stdafx.h"
#include "../debug.h"
#include "../newgrf_extension.h"
#include "../newgrf_text.h"
#include "../core/container_func.hpp"
#include "newgrf_bytereader.h"
#include "newgrf_internal.h"

#include "../table/strings.h"

#include "../safeguards.h"

/** Callback function for 'INFO'->'NAME' to add a translation to the newgrf name. */
static bool ChangeGRFName(uint8_t langid, std::string_view str)
{
	AddGRFTextToList(_cur.grfconfig->name, langid, _cur.grfconfig->ident.grfid, false, str);
	return true;
}

/** Callback function for 'INFO'->'DESC' to add a translation to the newgrf description. */
static bool ChangeGRFDescription(uint8_t langid, std::string_view str)
{
	AddGRFTextToList(_cur.grfconfig->info, langid, _cur.grfconfig->ident.grfid, true, str);
	return true;
}

/** Callback function for 'INFO'->'URL_' to set the newgrf url. */
static bool ChangeGRFURL(uint8_t langid, std::string_view str)
{
	AddGRFTextToList(_cur.grfconfig->url, langid, _cur.grfconfig->ident.grfid, false, str);
	return true;
}

/** Callback function for 'INFO'->'NPAR' to set the number of valid parameters. */
static bool ChangeGRFNumUsedParams(size_t len, ByteReader &buf)
{
	if (len != 1) {
		GrfMsg(2, "StaticGRFInfo: expected only 1 byte for 'INFO'->'NPAR' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		_cur.grfconfig->num_valid_params = std::min(buf.ReadByte(), GRFConfig::MAX_NUM_PARAMS);
	}
	return true;
}

/** Callback function for 'INFO'->'PALS' to set the number of valid parameters. */
static bool ChangeGRFPalette(size_t len, ByteReader &buf)
{
	if (len != 1) {
		GrfMsg(2, "StaticGRFInfo: expected only 1 byte for 'INFO'->'PALS' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		char data = buf.ReadByte();
		GRFPalette pal = GRFP_GRF_UNSET;
		switch (data) {
			case '*':
			case 'A': pal = GRFP_GRF_ANY;     break;
			case 'W': pal = GRFP_GRF_WINDOWS; break;
			case 'D': pal = GRFP_GRF_DOS;     break;
			default:
				GrfMsg(2, "StaticGRFInfo: unexpected value '{:02X}' for 'INFO'->'PALS', ignoring this field", data);
				break;
		}
		if (pal != GRFP_GRF_UNSET) {
			_cur.grfconfig->palette &= ~GRFP_GRF_MASK;
			_cur.grfconfig->palette |= pal;
		}
	}
	return true;
}

/** Callback function for 'INFO'->'BLTR' to set the blitter info. */
static bool ChangeGRFBlitter(size_t len, ByteReader &buf)
{
	if (len != 1) {
		GrfMsg(2, "StaticGRFInfo: expected only 1 byte for 'INFO'->'BLTR' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		char data = buf.ReadByte();
		GRFPalette pal = GRFP_BLT_UNSET;
		switch (data) {
			case '8': pal = GRFP_BLT_UNSET; break;
			case '3': pal = GRFP_BLT_32BPP;  break;
			default:
				GrfMsg(2, "StaticGRFInfo: unexpected value '{:02X}' for 'INFO'->'BLTR', ignoring this field", data);
				return true;
		}
		_cur.grfconfig->palette &= ~GRFP_BLT_MASK;
		_cur.grfconfig->palette |= pal;
	}
	return true;
}

/** Callback function for 'INFO'->'VRSN' to the version of the NewGRF. */
static bool ChangeGRFVersion(size_t len, ByteReader &buf)
{
	if (len != 4) {
		GrfMsg(2, "StaticGRFInfo: expected 4 bytes for 'INFO'->'VRSN' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		/* Set min_loadable_version as well (default to minimal compatibility) */
		_cur.grfconfig->version = _cur.grfconfig->min_loadable_version = buf.ReadDWord();
	}
	return true;
}

/** Callback function for 'INFO'->'MINV' to the minimum compatible version of the NewGRF. */
static bool ChangeGRFMinVersion(size_t len, ByteReader &buf)
{
	if (len != 4) {
		GrfMsg(2, "StaticGRFInfo: expected 4 bytes for 'INFO'->'MINV' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		_cur.grfconfig->min_loadable_version = buf.ReadDWord();
		if (_cur.grfconfig->version == 0) {
			GrfMsg(2, "StaticGRFInfo: 'MINV' defined before 'VRSN' or 'VRSN' set to 0, ignoring this field");
			_cur.grfconfig->min_loadable_version = 0;
		}
		if (_cur.grfconfig->version < _cur.grfconfig->min_loadable_version) {
			GrfMsg(2, "StaticGRFInfo: 'MINV' defined as {}, limiting it to 'VRSN'", _cur.grfconfig->min_loadable_version);
			_cur.grfconfig->min_loadable_version = _cur.grfconfig->version;
		}
	}
	return true;
}

static GRFParameterInfo *_cur_parameter; ///< The parameter which info is currently changed by the newgrf.

/** Callback function for 'INFO'->'PARAM'->param_num->'NAME' to set the name of a parameter. */
static bool ChangeGRFParamName(uint8_t langid, std::string_view str)
{
	AddGRFTextToList(_cur_parameter->name, langid, _cur.grfconfig->ident.grfid, false, str);
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'DESC' to set the description of a parameter. */
static bool ChangeGRFParamDescription(uint8_t langid, std::string_view str)
{
	AddGRFTextToList(_cur_parameter->desc, langid, _cur.grfconfig->ident.grfid, true, str);
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'TYPE' to set the typeof a parameter. */
static bool ChangeGRFParamType(size_t len, ByteReader &buf)
{
	if (len != 1) {
		GrfMsg(2, "StaticGRFInfo: expected 1 byte for 'INFO'->'PARA'->'TYPE' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		uint8_t type = buf.ReadByte();
		if (type < PTYPE_END) {
			_cur_parameter->type = (GRFParameterType)type;
		} else {
			GrfMsg(3, "StaticGRFInfo: unknown parameter type {}, ignoring this field", type);
		}
	}
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'LIMI' to set the min/max value of a parameter. */
static bool ChangeGRFParamLimits(size_t len, ByteReader &buf)
{
	if (_cur_parameter->type != PTYPE_UINT_ENUM) {
		GrfMsg(2, "StaticGRFInfo: 'INFO'->'PARA'->'LIMI' is only valid for parameters with type uint/enum, ignoring this field");
		buf.Skip(len);
	} else if (len != 8) {
		GrfMsg(2, "StaticGRFInfo: expected 8 bytes for 'INFO'->'PARA'->'LIMI' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		uint32_t min_value = buf.ReadDWord();
		uint32_t max_value = buf.ReadDWord();
		if (min_value <= max_value) {
			_cur_parameter->min_value = min_value;
			_cur_parameter->max_value = max_value;
		} else {
			GrfMsg(2, "StaticGRFInfo: 'INFO'->'PARA'->'LIMI' values are incoherent, ignoring this field");
		}
	}
	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'MASK' to set the parameter and bits to use. */
static bool ChangeGRFParamMask(size_t len, ByteReader &buf)
{
	if (len < 1 || len > 3) {
		GrfMsg(2, "StaticGRFInfo: expected 1 to 3 bytes for 'INFO'->'PARA'->'MASK' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		uint8_t param_nr = buf.ReadByte();
		if (param_nr >= GRFConfig::MAX_NUM_PARAMS) {
			GrfMsg(2, "StaticGRFInfo: invalid parameter number in 'INFO'->'PARA'->'MASK', param {}, ignoring this field", param_nr);
			buf.Skip(len - 1);
		} else {
			_cur_parameter->param_nr = param_nr;
			if (len >= 2) _cur_parameter->first_bit = std::min<uint8_t>(buf.ReadByte(), 31);
			if (len >= 3) _cur_parameter->num_bit = std::min<uint8_t>(buf.ReadByte(), 32 - _cur_parameter->first_bit);
		}
	}

	return true;
}

/** Callback function for 'INFO'->'PARAM'->param_num->'DFLT' to set the default value. */
static bool ChangeGRFParamDefault(size_t len, ByteReader &buf)
{
	if (len != 4) {
		GrfMsg(2, "StaticGRFInfo: expected 4 bytes for 'INFO'->'PARA'->'DEFA' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		_cur_parameter->def_value = buf.ReadDWord();
	}
	_cur.grfconfig->has_param_defaults = true;
	return true;
}

typedef bool (*DataHandler)(size_t, ByteReader &);          ///< Type of callback function for binary nodes
typedef bool (*TextHandler)(uint8_t, std::string_view str); ///< Type of callback function for text nodes
typedef bool (*BranchHandler)(ByteReader &);                ///< Type of callback function for branch nodes

/**
 * Data structure to store the allowed id/type combinations for action 14. The
 * data can be represented as a tree with 3 types of nodes:
 * 1. Branch nodes (identified by 'C' for choice).
 * 2. Binary leaf nodes (identified by 'B').
 * 3. Text leaf nodes (identified by 'T').
 */
struct AllowedSubtags {
	/** Custom 'span' of subtags. Required because std::span with an incomplete type is UB. */
	using Span = std::pair<const AllowedSubtags *, const AllowedSubtags *>;

	uint32_t id; ///< The identifier for this node.
	std::variant<DataHandler, TextHandler, BranchHandler, Span> handler; ///< The handler for this node.
};

static bool SkipUnknownInfo(ByteReader &buf, uint8_t type);
static bool HandleNodes(ByteReader &buf, std::span<const AllowedSubtags> tags);

/**
 * Try to skip the current branch node and all subnodes.
 * This is suitable for use with AllowedSubtags.
 * @param buf Buffer.
 * @return True if we could skip the node, false if an error occurred.
 */
static bool SkipInfoChunk(ByteReader &buf)
{
	uint8_t type = buf.ReadByte();
	while (type != 0) {
		buf.ReadDWord(); // chunk ID
		if (!SkipUnknownInfo(buf, type)) return false;
		type = buf.ReadByte();
	}
	return true;
}

/**
 * Callback function for 'INFO'->'PARA'->param_num->'VALU' to set the names
 * of some parameter values (type uint/enum) or the names of some bits
 * (type bitmask). In both cases the format is the same:
 * Each subnode should be a text node with the value/bit number as id.
 */
static bool ChangeGRFParamValueNames(ByteReader &buf)
{
	uint8_t type = buf.ReadByte();
	while (type != 0) {
		uint32_t id = buf.ReadDWord();
		if (type != 'T' || id > _cur_parameter->max_value) {
			GrfMsg(2, "StaticGRFInfo: all child nodes of 'INFO'->'PARA'->param_num->'VALU' should have type 't' and the value/bit number as id");
			if (!SkipUnknownInfo(buf, type)) return false;
			type = buf.ReadByte();
			continue;
		}

		uint8_t langid = buf.ReadByte();
		std::string_view name_string = buf.ReadString();

		auto it = std::ranges::lower_bound(_cur_parameter->value_names, id, std::less{}, &GRFParameterInfo::ValueName::first);
		if (it == std::end(_cur_parameter->value_names) || it->first != id) {
			it = _cur_parameter->value_names.emplace(it, id, GRFTextList{});
		}
		AddGRFTextToList(it->second, langid, _cur.grfconfig->ident.grfid, false, name_string);

		type = buf.ReadByte();
	}
	return true;
}

/** Action14 parameter tags */
static constexpr AllowedSubtags _tags_parameters[] = {
	AllowedSubtags{'NAME', ChangeGRFParamName},
	AllowedSubtags{'DESC', ChangeGRFParamDescription},
	AllowedSubtags{'TYPE', ChangeGRFParamType},
	AllowedSubtags{'LIMI', ChangeGRFParamLimits},
	AllowedSubtags{'MASK', ChangeGRFParamMask},
	AllowedSubtags{'VALU', ChangeGRFParamValueNames},
	AllowedSubtags{'DFLT', ChangeGRFParamDefault},
};

/**
 * Callback function for 'INFO'->'PARA' to set extra information about the
 * parameters. Each subnode of 'INFO'->'PARA' should be a branch node with
 * the parameter number as id. The first parameter has id 0. The maximum
 * parameter that can be changed is set by 'INFO'->'NPAR' which defaults to 80.
 */
static bool HandleParameterInfo(ByteReader &buf)
{
	uint8_t type = buf.ReadByte();
	while (type != 0) {
		uint32_t id = buf.ReadDWord();
		if (type != 'C' || id >= _cur.grfconfig->num_valid_params) {
			GrfMsg(2, "StaticGRFInfo: all child nodes of 'INFO'->'PARA' should have type 'C' and their parameter number as id");
			if (!SkipUnknownInfo(buf, type)) return false;
			type = buf.ReadByte();
			continue;
		}

		if (id >= _cur.grfconfig->param_info.size()) {
			_cur.grfconfig->param_info.resize(id + 1);
		}
		if (!_cur.grfconfig->param_info[id].has_value()) {
			_cur.grfconfig->param_info[id] = GRFParameterInfo(id);
		}
		_cur_parameter = &_cur.grfconfig->param_info[id].value();
		/* Read all parameter-data and process each node. */
		if (!HandleNodes(buf, _tags_parameters)) return false;
		type = buf.ReadByte();
	}
	return true;
}

/** Action14 tags for the INFO node */
static constexpr AllowedSubtags _tags_info[] = {
	AllowedSubtags{'NAME', ChangeGRFName},
	AllowedSubtags{'DESC', ChangeGRFDescription},
	AllowedSubtags{'URL_', ChangeGRFURL},
	AllowedSubtags{'NPAR', ChangeGRFNumUsedParams},
	AllowedSubtags{'PALS', ChangeGRFPalette},
	AllowedSubtags{'BLTR', ChangeGRFBlitter},
	AllowedSubtags{'VRSN', ChangeGRFVersion},
	AllowedSubtags{'MINV', ChangeGRFMinVersion},
	AllowedSubtags{'PARA', HandleParameterInfo},
};


/** Action14 feature test instance */
struct GRFFeatureTest {
	const GRFFeatureInfo *feature;
	uint16_t min_version;
	uint16_t max_version;
	uint8_t platform_var_bit;
	uint32_t test_91_value;

	void Reset()
	{
		this->feature = nullptr;
		this->min_version = 1;
		this->max_version = UINT16_MAX;
		this->platform_var_bit = 0;
		this->test_91_value = 0;
	}

	void ExecuteTest()
	{
		uint16_t version = (this->feature != nullptr) ? this->feature->version : 0;
		bool has_feature = (version >= this->min_version && version <= this->max_version);
		if (this->platform_var_bit > 0) {
			AssignBit(_cur.grffile->var9D_overlay, this->platform_var_bit, has_feature);
			GrfMsg(2, "Action 14 feature test: feature test: setting bit {} of var 0x9D to {}, {}", platform_var_bit, has_feature ? 1 : 0, _cur.grffile->var9D_overlay);
		}
		if (this->test_91_value > 0) {
			if (has_feature) {
				GrfMsg(2, "Action 14 feature test: feature test: adding test value 0x{:X} to var 0x91", this->test_91_value);
				include(_cur.grffile->var91_values, this->test_91_value);
			} else {
				GrfMsg(2, "Action 14 feature test: feature test: not adding test value 0x{:X} to var 0x91", this->test_91_value);
			}
		}
		if (this->platform_var_bit == 0 && this->test_91_value == 0) {
			GrfMsg(2, "Action 14 feature test: feature test: doing nothing: {}", has_feature ? 1 : 0);
		}
		if (this->feature != nullptr && this->feature->observation_flag != GFTOF_INVALID) {
			SetBit(_cur.grffile->observed_feature_tests, this->feature->observation_flag);
		}
	}
};

static GRFFeatureTest _current_grf_feature_test;

/** Callback function for 'FTST'->'NAME' to set the name of the feature being tested. */
static bool ChangeGRFFeatureTestName(uint8_t langid, std::string_view str)
{
	extern const GRFFeatureInfo _grf_feature_list[];
	for (const GRFFeatureInfo *info = _grf_feature_list; info->name != nullptr; info++) {
		if (str == info->name) {
			_current_grf_feature_test.feature = info;
			GrfMsg(2, "Action 14 feature test: found feature named: '{}' (version: {}) in 'FTST'->'NAME'", StrMakeValid(str), info->version);
			return true;
		}
	}
	GrfMsg(2, "Action 14 feature test: could not find feature named: '{}' in 'FTST'->'NAME'", StrMakeValid(str));
	_current_grf_feature_test.feature = nullptr;
	return true;
}

/** Callback function for 'FTST'->'MINV' to set the minimum version of the feature being tested. */
static bool ChangeGRFFeatureMinVersion(size_t len, ByteReader &buf)
{
	if (len != 2) {
		GrfMsg(2, "Action 14 feature test: expected 2 bytes for 'FTST'->'MINV' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		_current_grf_feature_test.min_version = buf.ReadWord();
	}
	return true;
}

/** Callback function for 'FTST'->'MAXV' to set the maximum version of the feature being tested. */
static bool ChangeGRFFeatureMaxVersion(size_t len, ByteReader &buf)
{
	if (len != 2) {
		GrfMsg(2, "Action 14 feature test: expected 2 bytes for 'FTST'->'MAXV' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		_current_grf_feature_test.max_version = buf.ReadWord();
	}
	return true;
}

/** Callback function for 'FTST'->'SETP' to set the bit number of global variable 9D (platform version) to set/unset with the result of the feature test. */
static bool ChangeGRFFeatureSetPlatformVarBit(size_t len, ByteReader &buf)
{
	if (len != 1) {
		GrfMsg(2, "Action 14 feature test: expected 1 byte for 'FTST'->'SETP' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		uint8_t bit_number = buf.ReadByte();
		if (bit_number >= 4 && bit_number <= 31) {
			_current_grf_feature_test.platform_var_bit = bit_number;
		} else {
			GrfMsg(2, "Action 14 feature test: expected a bit number >= 4 and <= 32 for 'FTST'->'SETP' but got {}, ignoring this field", bit_number);
		}
	}
	return true;
}

/** Callback function for 'FTST'->'SVAL' to add a test success result value for checking using global variable 91. */
static bool ChangeGRFFeatureTestSuccessResultValue(size_t len, ByteReader &buf)
{
	if (len != 4) {
		GrfMsg(2, "Action 14 feature test: expected 4 bytes for 'FTST'->'SVAL' but got {}, ignoring this field", len);
		buf.Skip(len);
	} else {
		_current_grf_feature_test.test_91_value = buf.ReadDWord();
	}
	return true;
}

/** Action14 tags for the FTST node */
static constexpr AllowedSubtags _tags_ftst[] = {
	AllowedSubtags{'NAME', ChangeGRFFeatureTestName},
	AllowedSubtags{'MINV', ChangeGRFFeatureMinVersion},
	AllowedSubtags{'MAXV', ChangeGRFFeatureMaxVersion},
	AllowedSubtags{'SETP', ChangeGRFFeatureSetPlatformVarBit},
	AllowedSubtags{'SVAL', ChangeGRFFeatureTestSuccessResultValue},
};

/**
 * Callback function for 'FTST' (feature test)
 */
static bool HandleFeatureTestInfo(ByteReader &buf)
{
	_current_grf_feature_test.Reset();
	HandleNodes(buf, _tags_ftst);
	_current_grf_feature_test.ExecuteTest();
	return true;
}

/** Action14 Action0 property map action instance */
struct GRFPropertyMapAction {
	const char *tag_name = nullptr;
	const char *descriptor = nullptr;

	GrfSpecFeature feature;
	int prop_id;
	int ext_prop_id;
	std::string name;
	GRFPropertyMapFallbackMode fallback_mode;
	uint8_t ttd_ver_var_bit;
	uint32_t test_91_value;
	uint8_t input_shift;
	uint8_t output_shift;
	uint input_mask;
	uint output_mask;
	uint output_param;

	void Reset(const char *tag, const char *desc)
	{
		this->tag_name = tag;
		this->descriptor = desc;

		this->feature = GSF_INVALID;
		this->prop_id = -1;
		this->ext_prop_id = -1;
		this->name.clear();
		this->fallback_mode = GPMFM_IGNORE;
		this->ttd_ver_var_bit = 0;
		this->test_91_value = 0;
		this->input_shift = 0;
		this->output_shift = 0;
		this->input_mask = 0;
		this->output_mask = 0;
		this->output_param = 0;
	}

	void ExecuteFeatureIDRemapping()
	{
		if (this->prop_id < 0) {
			GrfMsg(2, "Action 14 {} remapping: no feature ID defined, doing nothing", this->descriptor);
			return;
		}
		if (this->name.empty()) {
			GrfMsg(2, "Action 14 {} remapping: no name defined, doing nothing", this->descriptor);
			return;
		}
		SetBit(_cur.grffile->ctrl_flags, GFCF_HAVE_FEATURE_ID_REMAP);
		bool success = false;
		const char *str = this->name.c_str();
		extern const GRFFeatureMapDefinition _grf_remappable_features[];
		for (const GRFFeatureMapDefinition *info = _grf_remappable_features; info->name != nullptr; info++) {
			if (strcmp(info->name, str) == 0) {
				GRFFeatureMapRemapEntry &entry = _cur.grffile->feature_id_remaps.Entry(this->prop_id);
				entry.name = info->name;
				entry.feature = info->feature;
				entry.raw_id = this->prop_id;
				success = true;
				break;
			}
		}
		if (this->ttd_ver_var_bit > 0) {
			AssignBit(_cur.grffile->var8D_overlay, this->ttd_ver_var_bit, success);
		}
		if (this->test_91_value > 0 && success) {
			include(_cur.grffile->var91_values, this->test_91_value);
		}
		if (!success) {
			if (this->fallback_mode == GPMFM_ERROR_ON_DEFINITION) {
				GrfMsg(0, "Error: Unimplemented mapped {}: {}, mapped to: 0x{:02X}", this->descriptor, str, this->prop_id);
				GRFError *error = DisableGrf(STR_NEWGRF_ERROR_UNIMPLEMETED_MAPPED_FEATURE_ID);
				error->data = stredup(str);
				error->param_value[1] = GSF_INVALID;
				error->param_value[2] = this->prop_id;
			} else {
				const char *str_store = stredup(str);
				GrfMsg(2, "Unimplemented mapped {}: {}, mapped to: {:X}, {} on use",
						this->descriptor, str, this->prop_id, (this->fallback_mode == GPMFM_IGNORE) ? "ignoring" : "error");
				_cur.grffile->remap_unknown_property_names.emplace_back(str_store);
				GRFFeatureMapRemapEntry &entry = _cur.grffile->feature_id_remaps.Entry(this->prop_id);
				entry.name = str_store;
				entry.feature = (this->fallback_mode == GPMFM_IGNORE) ? GSF_INVALID : GSF_ERROR_ON_USE;
				entry.raw_id = this->prop_id;
			}
		}
	}

	void ExecutePropertyRemapping()
	{
		if (this->feature == GSF_INVALID) {
			GrfMsg(2, "Action 14 {} remapping: no feature defined, doing nothing", this->descriptor);
			return;
		}
		if (this->prop_id < 0 && this->ext_prop_id < 0) {
			GrfMsg(2, "Action 14 {} remapping: no property ID defined, doing nothing", this->descriptor);
			return;
		}
		if (this->name.empty()) {
			GrfMsg(2, "Action 14 {} remapping: no name defined, doing nothing", this->descriptor);
			return;
		}
		bool success = false;
		const char *str = this->name.c_str();
		extern const GRFPropertyMapDefinition _grf_action0_remappable_properties[];
		for (const GRFPropertyMapDefinition *info = _grf_action0_remappable_properties; info->name != nullptr; info++) {
			if ((info->feature == GSF_INVALID || info->feature == this->feature) && strcmp(info->name, str) == 0) {
				if (this->prop_id > 0) {
					GRFFilePropertyRemapEntry &entry = _cur.grffile->action0_property_remaps[this->feature].Entry(this->prop_id);
					entry.name = info->name;
					entry.id = info->id;
					entry.feature = this->feature;
					entry.property_id = this->prop_id;
				}
				if (this->ext_prop_id > 0) {
					GRFFilePropertyRemapEntry &entry = _cur.grffile->action0_extended_property_remaps[(((uint32_t)this->feature) << 16) | this->ext_prop_id];
					entry.name = info->name;
					entry.id = info->id;
					entry.feature = this->feature;
					entry.extended = true;
					entry.property_id = this->ext_prop_id;
				}
				success = true;
				break;
			}
		}
		if (this->ttd_ver_var_bit > 0) {
			AssignBit(_cur.grffile->var8D_overlay, this->ttd_ver_var_bit, success);
		}
		if (this->test_91_value > 0 && success) {
			include(_cur.grffile->var91_values, this->test_91_value);
		}
		if (!success) {
			uint mapped_to = (this->prop_id > 0) ? this->prop_id : this->ext_prop_id;
			const char *extended = (this->prop_id > 0) ? "" : " (extended)";
			if (this->fallback_mode == GPMFM_ERROR_ON_DEFINITION) {
				GrfMsg(0, "Error: Unimplemented mapped {}: {}, feature: {}, mapped to: {:X}{}", this->descriptor, str, GetFeatureString(this->feature), mapped_to, extended);
				GRFError *error = DisableGrf(STR_NEWGRF_ERROR_UNIMPLEMETED_MAPPED_PROPERTY);
				error->data = stredup(str);
				error->param_value[1] = this->feature;
				error->param_value[2] = ((this->prop_id > 0) ? 0 : 0xE0000) | mapped_to;
			} else {
				const char *str_store = stredup(str);
				GrfMsg(2, "Unimplemented mapped {}: {}, feature: {}, mapped to: {:X}{}, {} on use",
						this->descriptor, str, GetFeatureString(this->feature), mapped_to, extended, (this->fallback_mode == GPMFM_IGNORE) ? "ignoring" : "error");
				_cur.grffile->remap_unknown_property_names.emplace_back(str_store);
				if (this->prop_id > 0) {
					GRFFilePropertyRemapEntry &entry = _cur.grffile->action0_property_remaps[this->feature].Entry(this->prop_id);
					entry.name = str_store;
					entry.id = (this->fallback_mode == GPMFM_IGNORE) ? A0RPI_UNKNOWN_IGNORE : A0RPI_UNKNOWN_ERROR;
					entry.feature = this->feature;
					entry.property_id = this->prop_id;
				}
				if (this->ext_prop_id > 0) {
					GRFFilePropertyRemapEntry &entry = _cur.grffile->action0_extended_property_remaps[(((uint32_t)this->feature) << 16) | this->ext_prop_id];
					entry.name = str_store;
					entry.id = (this->fallback_mode == GPMFM_IGNORE) ? A0RPI_UNKNOWN_IGNORE : A0RPI_UNKNOWN_ERROR;;
					entry.feature = this->feature;
					entry.extended = true;
					entry.property_id = this->ext_prop_id;
				}
			}
		}
	}

	void ExecuteVariableRemapping()
	{
		if (this->feature == GSF_INVALID) {
			GrfMsg(2, "Action 14 {} remapping: no feature defined, doing nothing", this->descriptor);
			return;
		}
		if (this->name.empty()) {
			GrfMsg(2, "Action 14 {} remapping: no name defined, doing nothing", this->descriptor);
			return;
		}
		bool success = false;
		const char *str = this->name.c_str();
		extern const GRFVariableMapDefinition _grf_action2_remappable_variables[];
		for (const GRFVariableMapDefinition *info = _grf_action2_remappable_variables; info->name != nullptr; info++) {
			if (info->feature == this->feature && strcmp(info->name, str) == 0) {
				_cur.grffile->grf_variable_remaps.push_back({ (uint16_t)info->id, (uint8_t)this->feature, this->input_shift, this->output_shift, this->input_mask, this->output_mask, this->output_param });
				success = true;
				break;
			}
		}
		if (this->ttd_ver_var_bit > 0) {
			AssignBit(_cur.grffile->var8D_overlay, this->ttd_ver_var_bit, success);
		}
		if (this->test_91_value > 0 && success) {
			include(_cur.grffile->var91_values, this->test_91_value);
		}
		if (!success) {
			GrfMsg(2, "Unimplemented mapped {}: {}, feature: {}, mapped to 0", this->descriptor, str, GetFeatureString(this->feature));
		}
	}

	void ExecuteAction5TypeRemapping()
	{
		if (this->prop_id < 0) {
			GrfMsg(2, "Action 14 {} remapping: no type ID defined, doing nothing", this->descriptor);
			return;
		}
		if (this->name.empty()) {
			GrfMsg(2, "Action 14 {} remapping: no name defined, doing nothing", this->descriptor);
			return;
		}
		bool success = false;
		const char *str = this->name.c_str();
		extern const Action5TypeRemapDefinition _grf_action5_remappable_types[];
		for (const Action5TypeRemapDefinition *info = _grf_action5_remappable_types; info->name != nullptr; info++) {
			if (strcmp(info->name, str) == 0) {
				Action5TypeRemapEntry &entry = _cur.grffile->action5_type_remaps.Entry(this->prop_id);
				entry.name = info->name;
				entry.info = &(info->info);
				entry.type_id = this->prop_id;
				success = true;
				break;
			}
		}
		if (this->ttd_ver_var_bit > 0) {
			AssignBit(_cur.grffile->var8D_overlay, this->ttd_ver_var_bit, success);
		}
		if (this->test_91_value > 0 && success) {
			include(_cur.grffile->var91_values, this->test_91_value);
		}
		if (!success) {
			if (this->fallback_mode == GPMFM_ERROR_ON_DEFINITION) {
				GrfMsg(0, "Error: Unimplemented mapped {}: {}, mapped to: {:X}", this->descriptor, str, this->prop_id);
				GRFError *error = DisableGrf(STR_NEWGRF_ERROR_UNIMPLEMETED_MAPPED_ACTION5_TYPE);
				error->data = stredup(str);
				error->param_value[1] = this->prop_id;
			} else {
				const char *str_store = stredup(str);
				GrfMsg(2, "Unimplemented mapped {}: {}, mapped to: {:X}, {} on use",
						this->descriptor, str, this->prop_id, (this->fallback_mode == GPMFM_IGNORE) ? "ignoring" : "error");
				_cur.grffile->remap_unknown_property_names.emplace_back(str_store);
				Action5TypeRemapEntry &entry = _cur.grffile->action5_type_remaps.Entry(this->prop_id);
				entry.name = str_store;
				entry.info = nullptr;
				entry.type_id = this->prop_id;
				entry.fallback_mode = this->fallback_mode;
			}
		}
	}
};

static GRFPropertyMapAction _current_grf_property_map_action;

/** Callback function for ->'NAME' to set the name of the item to be mapped. */
static bool ChangePropertyRemapName(uint8_t langid, std::string_view str)
{
	_current_grf_property_map_action.name = str;
	return true;
}

/** Callback function for ->'FEAT' to set which feature this mapping applies to. */
static bool ChangePropertyRemapFeature(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'FEAT' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		GrfSpecFeatureRef feature = ReadFeature(buf.ReadByte());
		if (feature.id >= GSF_END) {
			GrfMsg(2, "Action 14 {} mapping: invalid feature ID: {}, in '{}'->'FEAT', ignoring this field", action.descriptor, GetFeatureString(feature), action.tag_name);
		} else {
			action.feature = feature.id;
		}
	}
	return true;
}

/** Callback function for ->'PROP' to set the property ID to which this item is being mapped. */
static bool ChangePropertyRemapPropertyId(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'PROP' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		action.prop_id = buf.ReadByte();
	}
	return true;
}

/** Callback function for ->'XPRP' to set the extended property ID to which this item is being mapped. */
static bool ChangePropertyRemapExtendedPropertyId(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 2) {
		GrfMsg(2, "Action 14 {} mapping: expected 2 bytes for '{}'->'XPRP' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		action.ext_prop_id = buf.ReadWord();
	}
	return true;
}

/** Callback function for ->'FTID' to set the feature ID to which this feature is being mapped. */
static bool ChangePropertyRemapFeatureId(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'FTID' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		action.prop_id = buf.ReadByte();
	}
	return true;
}

/** Callback function for ->'TYPE' to set the property ID to which this item is being mapped. */
static bool ChangePropertyRemapTypeId(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'TYPE' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		uint8_t prop = buf.ReadByte();
		if (prop < 128) {
			action.prop_id = prop;
		} else {
			GrfMsg(2, "Action 14 {} mapping: expected a type < 128 for '{}'->'TYPE' but got {}, ignoring this field", action.descriptor, action.tag_name, prop);
		}
	}
	return true;
}

/** Callback function for ->'FLBK' to set the fallback mode. */
static bool ChangePropertyRemapSetFallbackMode(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'FLBK' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		GRFPropertyMapFallbackMode mode = (GRFPropertyMapFallbackMode) buf.ReadByte();
		if (mode < GPMFM_END) action.fallback_mode = mode;
	}
	return true;
}
/** Callback function for ->'SETT' to set the bit number of global variable 8D (TTD version) to set/unset with whether the remapping was successful. */
static bool ChangePropertyRemapSetTTDVerVarBit(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'SETT' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		uint8_t bit_number = buf.ReadByte();
		if (bit_number >= 4 && bit_number <= 31) {
			action.ttd_ver_var_bit = bit_number;
		} else {
			GrfMsg(2, "Action 14 {} mapping: expected a bit number >= 4 and <= 32 for '{}'->'SETT' but got {}, ignoring this field", action.descriptor, action.tag_name, bit_number);
		}
	}
	return true;
}

/** Callback function for >'SVAL' to add a success result value for checking using global variable 91. */
static bool ChangePropertyRemapSuccessResultValue(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 4) {
		GrfMsg(2, "Action 14 {} mapping: expected 4 bytes for '{}'->'SVAL' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		action.test_91_value = buf.ReadDWord();
	}
	return true;
}

/** Callback function for ->'RSFT' to set the input shift value for variable remapping. */
static bool ChangePropertyRemapSetInputShift(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'RSFT' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		uint8_t input_shift = buf.ReadByte();
		if (input_shift < 0x20) {
			action.input_shift = input_shift;
		} else {
			GrfMsg(2, "Action 14 {} mapping: expected a shift value < 0x20 for '{}'->'RSFT' but got {}, ignoring this field", action.descriptor, action.tag_name, input_shift);
		}
	}
	return true;
}

/** Callback function for ->'VSFT' to set the output shift value for variable remapping. */
static bool ChangePropertyRemapSetOutputShift(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 1) {
		GrfMsg(2, "Action 14 {} mapping: expected 1 byte for '{}'->'VSFT' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		uint8_t output_shift = buf.ReadByte();
		if (output_shift < 0x20) {
			action.output_shift = output_shift;
		} else {
			GrfMsg(2, "Action 14 {} mapping: expected a shift value < 0x20 for '{}'->'VSFT' but got {}, ignoring this field", action.descriptor, action.tag_name, output_shift);
		}
	}
	return true;
}

/** Callback function for ->'RMSK' to set the input mask value for variable remapping. */
static bool ChangePropertyRemapSetInputMask(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 4) {
		GrfMsg(2, "Action 14 {} mapping: expected 4 bytes for '{}'->'RMSK' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		action.input_mask = buf.ReadDWord();
	}
	return true;
}

/** Callback function for ->'VMSK' to set the output mask value for variable remapping. */
static bool ChangePropertyRemapSetOutputMask(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 4) {
		GrfMsg(2, "Action 14 {} mapping: expected 4 bytes for '{}'->'VMSK' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		action.output_mask = buf.ReadDWord();
	}
	return true;
}

/** Callback function for ->'VPRM' to set the output parameter value for variable remapping. */
static bool ChangePropertyRemapSetOutputParam(size_t len, ByteReader &buf)
{
	GRFPropertyMapAction &action = _current_grf_property_map_action;
	if (len != 4) {
		GrfMsg(2, "Action 14 {} mapping: expected 4 bytes for '{}'->'VPRM' but got {}, ignoring this field", action.descriptor, action.tag_name, len);
		buf.Skip(len);
	} else {
		action.output_param = buf.ReadDWord();
	}
	return true;
}

/** Action14 tags for the FIDM node */
static constexpr AllowedSubtags _tags_fidm[] = {
	AllowedSubtags{'NAME', ChangePropertyRemapName},
	AllowedSubtags{'FTID', ChangePropertyRemapFeatureId},
	AllowedSubtags{'FLBK', ChangePropertyRemapSetFallbackMode},
	AllowedSubtags{'SETT', ChangePropertyRemapSetTTDVerVarBit},
	AllowedSubtags{'SVAL', ChangePropertyRemapSuccessResultValue},
};

/**
 * Callback function for 'FIDM' (feature ID mapping)
 */
static bool HandleFeatureIDMap(ByteReader &buf)
{
	_current_grf_property_map_action.Reset("FIDM", "feature");
	HandleNodes(buf, _tags_fidm);
	_current_grf_property_map_action.ExecuteFeatureIDRemapping();
	return true;
}

/** Action14 tags for the A0PM node */
static constexpr AllowedSubtags _tags_a0pm[] = {
	AllowedSubtags{'NAME', ChangePropertyRemapName},
	AllowedSubtags{'FEAT', ChangePropertyRemapFeature},
	AllowedSubtags{'PROP', ChangePropertyRemapPropertyId},
	AllowedSubtags{'XPRP', ChangePropertyRemapExtendedPropertyId},
	AllowedSubtags{'FLBK', ChangePropertyRemapSetFallbackMode},
	AllowedSubtags{'SETT', ChangePropertyRemapSetTTDVerVarBit},
	AllowedSubtags{'SVAL', ChangePropertyRemapSuccessResultValue},
};

/**
 * Callback function for 'A0PM' (action 0 property mapping)
 */
static bool HandleAction0PropertyMap(ByteReader &buf)
{
	_current_grf_property_map_action.Reset("A0PM", "property");
	HandleNodes(buf, _tags_a0pm);
	_current_grf_property_map_action.ExecutePropertyRemapping();
	return true;
}

/** Action14 tags for the A2VM node */
static constexpr AllowedSubtags _tags_a2vm[] = {
	AllowedSubtags{'NAME', ChangePropertyRemapName},
	AllowedSubtags{'FEAT', ChangePropertyRemapFeature},
	AllowedSubtags{'RSFT', ChangePropertyRemapSetInputShift},
	AllowedSubtags{'RMSK', ChangePropertyRemapSetInputMask},
	AllowedSubtags{'VSFT', ChangePropertyRemapSetOutputShift},
	AllowedSubtags{'VMSK', ChangePropertyRemapSetOutputMask},
	AllowedSubtags{'VPRM', ChangePropertyRemapSetOutputParam},
	AllowedSubtags{'SETT', ChangePropertyRemapSetTTDVerVarBit},
	AllowedSubtags{'SVAL', ChangePropertyRemapSuccessResultValue},
};

/**
 * Callback function for 'A2VM' (action 2 variable mapping)
 */
static bool HandleAction2VariableMap(ByteReader &buf)
{
	_current_grf_property_map_action.Reset("A2VM", "variable");
	HandleNodes(buf, _tags_a2vm);
	_current_grf_property_map_action.ExecuteVariableRemapping();
	return true;
}

/** Action14 tags for the A5TM node */
static constexpr AllowedSubtags _tags_a5tm[] = {
	AllowedSubtags{'NAME', ChangePropertyRemapName},
	AllowedSubtags{'TYPE', ChangePropertyRemapTypeId},
	AllowedSubtags{'FLBK', ChangePropertyRemapSetFallbackMode},
	AllowedSubtags{'SETT', ChangePropertyRemapSetTTDVerVarBit},
	AllowedSubtags{'SVAL', ChangePropertyRemapSuccessResultValue},
};

/**
 * Callback function for 'A5TM' (action 5 type mapping)
 */
static bool HandleAction5TypeMap(ByteReader &buf)
{
	_current_grf_property_map_action.Reset("A5TM", "Action 5 type");
	HandleNodes(buf, _tags_a5tm);
	_current_grf_property_map_action.ExecuteAction5TypeRemapping();
	return true;
}

/** Action14 root tags */
static constexpr AllowedSubtags _tags_root_static[] = {
	AllowedSubtags{'INFO', std::make_pair(std::begin(_tags_info), std::end(_tags_info))},
	AllowedSubtags{'FTST', SkipInfoChunk},
	AllowedSubtags{'FIDM', SkipInfoChunk},
	AllowedSubtags{'A0PM', SkipInfoChunk},
	AllowedSubtags{'A2VM', SkipInfoChunk},
	AllowedSubtags{'A5TM', SkipInfoChunk},
};

/** Action14 root tags */
static constexpr AllowedSubtags _tags_root_feature_tests[] = {
	AllowedSubtags{'INFO', SkipInfoChunk},
	AllowedSubtags{'FTST', HandleFeatureTestInfo},
	AllowedSubtags{'FIDM', HandleFeatureIDMap},
	AllowedSubtags{'A0PM', HandleAction0PropertyMap},
	AllowedSubtags{'A2VM', HandleAction2VariableMap},
	AllowedSubtags{'A5TM', HandleAction5TypeMap},
};


/**
 * Try to skip the current node and all subnodes (if it's a branch node).
 * @param buf Buffer.
 * @param type The node type to skip.
 * @return True if we could skip the node, false if an error occurred.
 */
static bool SkipUnknownInfo(ByteReader &buf, uint8_t type)
{
	/* type and id are already read */
	switch (type) {
		case 'C': {
			uint8_t new_type = buf.ReadByte();
			while (new_type != 0) {
				buf.ReadDWord(); // skip the id
				if (!SkipUnknownInfo(buf, new_type)) return false;
				new_type = buf.ReadByte();
			}
			break;
		}

		case 'T':
			buf.ReadByte(); // lang
			buf.ReadString(); // actual text
			break;

		case 'B': {
			uint16_t size = buf.ReadWord();
			buf.Skip(size);
			break;
		}

		default:
			return false;
	}

	return true;
}

/**
 * Handle the nodes of an Action14
 * @param type Type of node.
 * @param id ID.
 * @param buf Buffer.
 * @param subtags Allowed subtags.
 * @return Whether all tags could be handled.
 */
static bool HandleNode(uint8_t type, uint32_t id, ByteReader &buf, std::span<const AllowedSubtags> subtags)
{
	/* Visitor to get a subtag handler's type. */
	struct type_visitor {
		char operator()(const DataHandler &) { return 'B'; }
		char operator()(const TextHandler &) { return 'T'; }
		char operator()(const BranchHandler &) { return 'C'; }
		char operator()(const AllowedSubtags::Span &) { return 'C'; }
	};

	/* Visitor to evaluate a subtag handler. */
	struct evaluate_visitor {
		ByteReader &buf;

		bool operator()(const DataHandler &handler)
		{
			size_t len = buf.ReadWord();
			if (buf.Remaining() < len) return false;
			return handler(len, buf);
		}

		bool operator()(const TextHandler &handler)
		{
			uint8_t langid = buf.ReadByte();
			return handler(langid, buf.ReadString());
		}

		bool operator()(const BranchHandler &handler)
		{
			return handler(buf);
		}

		bool operator()(const AllowedSubtags::Span &subtags)
		{
			return HandleNodes(buf, {subtags.first, subtags.second});
		}
	};

	for (const auto &tag : subtags) {
		if (tag.id != std::byteswap(id) || std::visit(type_visitor{}, tag.handler) != type) continue;
		return std::visit(evaluate_visitor{buf}, tag.handler);
	}

	GrfMsg(2, "StaticGRFInfo: unknown type/id combination found, type={:c}, id={:x}", type, id);
	return SkipUnknownInfo(buf, type);
}

/**
 * Handle the contents of a 'C' choice of an Action14
 * @param buf Buffer.
 * @param subtags List of subtags.
 * @return Whether the nodes could all be handled.
 */
static bool HandleNodes(ByteReader &buf, std::span<const AllowedSubtags> subtags)
{
	uint8_t type = buf.ReadByte();
	while (type != 0) {
		uint32_t id = buf.ReadDWord();
		if (!HandleNode(type, id, buf, subtags)) return false;
		type = buf.ReadByte();
	}
	return true;
}

/**
 * Handle Action 0x14 (static info)
 * @param buf Buffer.
 */
static void StaticGRFInfo(ByteReader &buf)
{
	/* <14> <type> <id> <text/data...> */
	HandleNodes(buf, _tags_root_static);
}

/**
 * Handle Action 0x14 (feature tests)
 * @param buf Buffer.
 */
static void Act14FeatureTest(ByteReader &buf)
{
	/* <14> <type> <id> <text/data...> */
	HandleNodes(buf, _tags_root_feature_tests);
}

template <> void GrfActionHandler<0x14>::FileScan(ByteReader &buf) { StaticGRFInfo(buf); }
template <> void GrfActionHandler<0x14>::SafetyScan(ByteReader &) { }
template <> void GrfActionHandler<0x14>::LabelScan(ByteReader &) { }
template <> void GrfActionHandler<0x14>::Init(ByteReader &buf) { Act14FeatureTest(buf); }
template <> void GrfActionHandler<0x14>::Reserve(ByteReader &) { }
template <> void GrfActionHandler<0x14>::Activation(ByteReader &) { }
