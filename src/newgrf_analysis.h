/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_analysis.h NewGRF analysis. */

#ifndef NEWGRF_ANALYSIS_H
#define NEWGRF_ANALYSIS_H

#include "newgrf_commons.h"
#include "newgrf_spritegroup.h"

#include "3rdparty/robin_hood/robin_hood.h"

struct IndustryTileDataAnalyserConfig;

enum AnalyseCallbackOperationMode : uint8_t {
	ACOM_CB_VAR,
	ACOM_CB36_PROP,
	ACOM_CB36_SPEED,
	ACOM_CB_REFIT_CAPACITY,
};

enum AnalyseCallbackOperationResultFlags : uint8_t {
	ACORF_NONE                              = 0,
	ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND  = 1 << 0,
	ACORF_CB_REFIT_CAP_SEEN_VAR_47          = 1 << 1,
};
DECLARE_ENUM_AS_BIT_SET(AnalyseCallbackOperationResultFlags)

struct BaseSpriteChainAnalyser {
	robin_hood::unordered_flat_set<const DeterministicSpriteGroup *> seen_dsg;

	bool RegisterSeenDeterministicSpriteGroup(const DeterministicSpriteGroup *);
	std::pair<bool, const SpriteGroup *> HandleGroupBypassing(const DeterministicSpriteGroup *);
};

template <typename T>
struct SpriteChainAnalyser : private BaseSpriteChainAnalyser {
	void AnalyseGroup(const SpriteGroup *);

	void DefaultAnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *);
	void DefaultAnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *);
};

struct FindCBResultAnalyser final : public SpriteChainAnalyser<FindCBResultAnalyser> {
	uint16_t callback;
	bool check_var_10;
	uint8_t var_10_value;

	bool found = false;

private:
	FindCBResultAnalyser(uint16_t callback, bool check_var_10, uint8_t var_10_value) : callback(callback), check_var_10(check_var_10), var_10_value(var_10_value) {}

public:
	bool IsEarlyExitSet() const { return this->found; }
	void AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *);
	void AnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *rsg) { this->DefaultAnalyseRandomisedSpriteGroup(rsg); }
	void AnalyseCallbackResultSpriteGroup(const CallbackResultSpriteGroup *);

	static bool Execute(const SpriteGroup *sg, uint16_t callback, bool check_var_10, uint8_t var_10_value)
	{
		FindCBResultAnalyser analyser(callback, check_var_10, var_10_value);
		analyser.AnalyseGroup(sg);
		return analyser.found;
	}
};

struct FindRandomTriggerAnalyser final : public SpriteChainAnalyser<FindRandomTriggerAnalyser> {
	bool found_trigger = false;

	bool IsEarlyExitSet() const { return this->found_trigger; }
	void AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *dsg);
	void AnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *);
	void AnalyseCallbackResultSpriteGroup(const CallbackResultSpriteGroup *) { /* Do nothing */ }
};

struct IndustryTileDataAnalyser final : public SpriteChainAnalyser<IndustryTileDataAnalyser> {
	const IndustryTileDataAnalyserConfig &cfg;
	uint64_t check_mask;
	bool anim_state_at_offset = false;

	IndustryTileDataAnalyser(const IndustryTileDataAnalyserConfig &cfg, uint64_t check_mask) : cfg(cfg), check_mask(check_mask) {}

	bool IsEarlyExitSet() const { return this->anim_state_at_offset; }
	void AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *);
	void AnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *rsg) { this->DefaultAnalyseRandomisedSpriteGroup(rsg); }
	void AnalyseCallbackResultSpriteGroup(const CallbackResultSpriteGroup *) { /* Do nothing */ }
};

struct CallbackOperationAnalyser final : public SpriteChainAnalyser<CallbackOperationAnalyser> {
	const AnalyseCallbackOperationMode mode;
	SpriteGroupCallbacksUsed callbacks_used = SGCU_NONE;
	AnalyseCallbackOperationResultFlags result_flags = ACORF_NONE;
	uint64_t cb36_properties_used = 0;

	CallbackOperationAnalyser(AnalyseCallbackOperationMode mode) : mode(mode) {}

	bool IsEarlyExitSet() const { return this->mode == ACOM_CB36_SPEED && (this->callbacks_used & SGCU_CB36_SPEED_RAILTYPE) != 0; }
	void AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *);
	void AnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *);
	void AnalyseCallbackResultSpriteGroup(const CallbackResultSpriteGroup *) { /* Do nothing */ }
};

#endif /* NEWGRF_ANALYSIS_H */
