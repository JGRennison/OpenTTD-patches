/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file programmable_signals.cpp Programmable Pre-Signals */

#include "stdafx.h"
#include "programmable_signals.h"
#include "debug.h"
#include "command_func.h"
#include "table/strings.h"
#include "window_func.h"
#include "company_func.h"
#include "cmd_helper.h"

ProgramList _signal_programs;
bool _cleaning_signal_programs = false;

SignalProgram::SignalProgram(TileIndex tile, Track track, bool raw)
{
	this->tile  = tile;
	this->track = track;
	if (!raw) {
		this->first_instruction = new SignalSpecial(this, PSO_FIRST);
		this->last_instruction  = new SignalSpecial(this, PSO_LAST);
		SignalSpecial::link(this->first_instruction, this->last_instruction);
	}
}

SignalProgram::~SignalProgram()
{
	this->first_instruction->Remove();
	delete this->first_instruction;
	delete this->last_instruction;
}

struct SignalVM {
	// Initial information
	uint num_exits;                 ///< Number of exits from block
	uint num_green;                 ///< Number of green exits from block
	SignalProgram *program;         ///< The program being run

	// Current state
	SignalInstruction *instruction; ///< Instruction to execute next

	// Output state
	SignalState state;

	void Execute()
	{
		DEBUG(misc, 6, "Begining execution of programmable pre-signal on tile %x, track %d",
					this->program->tile, this->program->track);
		do {
			DEBUG(misc, 10, "  Executing instruction %d, opcode %d", this->instruction->Id(), this->instruction->Opcode());
			this->instruction->Evaluate(*this);
		} while (this->instruction);

		DEBUG(misc, 6, "Completed");
	}
};

// -- Conditions

SignalCondition::~SignalCondition()
{}

SignalSimpleCondition::SignalSimpleCondition(SignalConditionCode code)
	: SignalCondition(code)
{}

/* virtual */ bool SignalSimpleCondition::Evaluate(SignalVM &vm)
{
	switch (this->cond_code) {
		case PSC_ALWAYS:    return true;
		case PSC_NEVER:     return false;
		default: NOT_REACHED();
	}
}

bool SignalConditionComparable::EvaluateComparable(uint32_t var_val)
{
	switch (this->comparator) {
		case SGC_EQUALS:            return var_val == this->value;
		case SGC_NOT_EQUALS:        return var_val != this->value;
		case SGC_LESS_THAN:         return var_val <  this->value;
		case SGC_LESS_THAN_EQUALS:  return var_val <= this->value;
		case SGC_MORE_THAN:         return var_val >  this->value;
		case SGC_MORE_THAN_EQUALS:  return var_val >= this->value;
		case SGC_IS_TRUE:           return var_val != 0;
		case SGC_IS_FALSE:          return !var_val;
		default: NOT_REACHED();
	}
}

SignalVariableCondition::SignalVariableCondition(SignalConditionCode code)
	: SignalConditionComparable(code)
{
	switch (this->cond_code) {
		case PSC_NUM_GREEN: comparator = SGC_NOT_EQUALS; break;
		case PSC_NUM_RED:   comparator = SGC_EQUALS; break;
		default: NOT_REACHED();
	}
	value = 0;
}

/*virtual*/ bool SignalVariableCondition::Evaluate(SignalVM &vm)
{
	uint32_t var_val;
	switch (this->cond_code) {
		case PSC_NUM_GREEN:  var_val = vm.num_green; break;
		case PSC_NUM_RED:    var_val = vm.num_exits - vm.num_green; break;
		default: NOT_REACHED();
	}
	return this->EvaluateComparable(var_val);
}

void AddSignalSlotDependency(TraceRestrictSlotID on, SignalReference dep)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::Get(on);
	assert(slot->owner == GetTileOwner(dep.tile));
	slot->progsig_dependants.push_back(dep);
}

void RemoveSignalSlotDependency(TraceRestrictSlotID on, SignalReference dep)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::Get(on);
	auto ob = std::find(slot->progsig_dependants.begin(), slot->progsig_dependants.end(), dep);

	if (ob != slot->progsig_dependants.end()) slot->progsig_dependants.erase(ob);
}

void AddSignalCounterDependency(TraceRestrictCounterID on, SignalReference dep)
{
	TraceRestrictCounter *ctr = TraceRestrictCounter::Get(on);
	assert(ctr->owner == GetTileOwner(dep.tile));
	ctr->progsig_dependants.push_back(dep);
}

void RemoveSignalCounterDependency(TraceRestrictCounterID on, SignalReference dep)
{
	TraceRestrictCounter *ctr = TraceRestrictCounter::Get(on);
	auto ob = std::find(ctr->progsig_dependants.begin(), ctr->progsig_dependants.end(), dep);

	if (ob != ctr->progsig_dependants.end()) ctr->progsig_dependants.erase(ob);
}

SignalSlotCondition::SignalSlotCondition(SignalConditionCode code, SignalReference this_sig, TraceRestrictSlotID slot_id)
	: SignalConditionComparable(code), this_sig(this_sig), slot_id(slot_id)
{
	this->comparator = SGC_EQUALS;
	this->value = 0;
	if (this->CheckSlotValid()) {
		AddSignalSlotDependency(this->slot_id, this->this_sig);
	}
}

bool SignalSlotCondition::IsSlotValid() const
{
	return TraceRestrictSlot::IsValidID(this->slot_id);
}

bool SignalSlotCondition::CheckSlotValid()
{
	bool valid = this->IsSlotValid();
	if (!valid) {
		this->Invalidate();
	}
	return valid;
}

void SignalSlotCondition::Invalidate()
{
	this->slot_id = INVALID_TRACE_RESTRICT_SLOT_ID;
}

void SignalSlotCondition::SetSlot(TraceRestrictSlotID slot_id)
{
	if (this->IsSlotValid()) {
		RemoveSignalSlotDependency(this->slot_id, this->this_sig);
	}
	this->slot_id = slot_id;
	if (this->CheckSlotValid()) {
		AddSignalSlotDependency(this->slot_id, this->this_sig);
	}
}

/*virtual*/ SignalSlotCondition::~SignalSlotCondition()
{
	if (_cleaning_signal_programs) return;

	if (this->IsSlotValid()) {
		RemoveSignalSlotDependency(this->slot_id, this->this_sig);
	}
}

/*virtual*/ bool SignalSlotCondition::Evaluate(SignalVM &vm)
{
	if (!this->CheckSlotValid()) {
		DEBUG(misc, 1, "Signal (%x, %d) has an invalid condition", this->this_sig.tile, this->this_sig.track);
		return false;
	}

	const TraceRestrictSlot *slot = TraceRestrictSlot::Get(this->slot_id);
	switch (this->cond_code) {
		case PSC_SLOT_OCC:     return this->EvaluateComparable((uint)slot->occupants.size());
		case PSC_SLOT_OCC_REM: return this->EvaluateComparable(slot->max_occupancy > (uint)slot->occupants.size() ? slot->max_occupancy - (uint)slot->occupants.size() : 0);
		default: NOT_REACHED();
	}
}

SignalCounterCondition::SignalCounterCondition(SignalReference this_sig, TraceRestrictCounterID ctr_id)
	: SignalConditionComparable(PSC_COUNTER), this_sig(this_sig), ctr_id(ctr_id)
{
	this->comparator = SGC_EQUALS;
	this->value = 0;
	if (this->CheckCounterValid()) {
		AddSignalCounterDependency(this->ctr_id, this->this_sig);
	}
}

bool SignalCounterCondition::IsCounterValid() const
{
	return TraceRestrictCounter::IsValidID(this->ctr_id);
}

bool SignalCounterCondition::CheckCounterValid()
{
	bool valid = this->IsCounterValid();
	if (!valid) {
		this->Invalidate();
	}
	return valid;
}

void SignalCounterCondition::Invalidate()
{
	this->ctr_id = INVALID_TRACE_RESTRICT_COUNTER_ID;
}

void SignalCounterCondition::SetCounter(TraceRestrictCounterID ctr_id)
{
	if (this->IsCounterValid()) {
		RemoveSignalCounterDependency(this->ctr_id, this->this_sig);
	}
	this->ctr_id = ctr_id;
	if (this->CheckCounterValid()) {
		AddSignalCounterDependency(this->ctr_id, this->this_sig);
	}
}

/*virtual*/ SignalCounterCondition::~SignalCounterCondition()
{
	if (_cleaning_signal_programs) return;

	if (this->IsCounterValid()) {
		RemoveSignalCounterDependency(this->ctr_id, this->this_sig);
	}
}

/*virtual*/ bool SignalCounterCondition::Evaluate(SignalVM &vm)
{
	if (!this->CheckCounterValid()) {
		DEBUG(misc, 1, "Signal (%x, %d) has an invalid condition", this->this_sig.tile, this->this_sig.track);
		return false;
	}

	return this->EvaluateComparable(TraceRestrictCounter::Get(this->ctr_id)->value);
}

SignalStateCondition::SignalStateCondition(SignalReference this_sig,
															TileIndex sig_tile, Trackdir sig_track)
	: SignalCondition(PSC_SIGNAL_STATE), this_sig(this_sig), sig_tile(sig_tile)
	, sig_track(sig_track)
{
	if (this->CheckSignalValid()) {
		AddSignalDependency(SignalReference(this->sig_tile, TrackdirToTrack(sig_track)), this->this_sig);
	}
}

bool SignalStateCondition::IsSignalValid() const
{
	return IsValidTile(this->sig_tile) && IsTileType(this->sig_tile, MP_RAILWAY) && HasSignalOnTrackdir(this->sig_tile, this->sig_track);
}

bool SignalStateCondition::CheckSignalValid()
{
	bool valid = this->IsSignalValid();
	if (!valid) {
		this->Invalidate();
	}
	return valid;
}

void SignalStateCondition::Invalidate()
{
	this->sig_tile = INVALID_TILE;
}

void SignalStateCondition::SetSignal(TileIndex tile, Trackdir track)
{
	if (this->IsSignalValid()) {
		RemoveSignalDependency(SignalReference(this->sig_tile, TrackdirToTrack(sig_track)), this->this_sig);
	}
	this->sig_tile = tile;
	this->sig_track = track;
	if (this->CheckSignalValid()) {
		AddSignalDependency(SignalReference(this->sig_tile, TrackdirToTrack(sig_track)), this->this_sig);
	}
}

/*virtual*/ SignalStateCondition::~SignalStateCondition()
{
	if (_cleaning_signal_programs) return;

	if (this->IsSignalValid()) {
		RemoveSignalDependency(SignalReference(this->sig_tile, TrackdirToTrack(sig_track)), this->this_sig);
	}
}

/*virtual*/ bool SignalStateCondition::Evaluate(SignalVM &vm)
{
	if (!this->CheckSignalValid()) {
		DEBUG(misc, 1, "Signal (%x, %d) has an invalid condition", this->this_sig.tile, this->this_sig.track);
		return false;
	}

	return GetSignalStateByTrackdir(this->sig_tile, this->sig_track) == SIGNAL_STATE_GREEN;
}

// -- Instructions
SignalInstruction::SignalInstruction(SignalProgram *prog, SignalOpcode op)
	: opcode(op), previous(nullptr), program(prog)
{
	program->instructions.push_back(this);
}

SignalInstruction::~SignalInstruction()
{
	auto pthis = std::find(program->instructions.begin(), program->instructions.end(), this);
	assert(pthis != program->instructions.end());
	program->instructions.erase(pthis);
}

void SignalInstruction::Insert(SignalInstruction *before_insn)
{
	this->previous = before_insn->Previous();
	before_insn->Previous()->SetNext(this);
	before_insn->SetPrevious(this);
	this->SetNext(before_insn);
}

SignalSpecial::SignalSpecial(SignalProgram *prog, SignalOpcode op)
	: SignalInstruction(prog, op)
{
	assert(op == PSO_FIRST || op == PSO_LAST);
	this->next = nullptr;
}

/*virtual*/ void SignalSpecial::Remove()
{
	if (opcode == PSO_FIRST) {
		while (this->next->Opcode() != PSO_LAST) this->next->Remove();
	} else if (opcode == PSO_LAST) {
	} else {
		NOT_REACHED();
	}
}

/*static*/ void SignalSpecial::link(SignalSpecial *first, SignalSpecial *last)
{
	assert(first->opcode == PSO_FIRST && last->opcode == PSO_LAST);
	first->next    = last;
	last->previous = first;
}

void SignalSpecial::Evaluate(SignalVM &vm)
{
	if (this->opcode == PSO_FIRST) {
		DEBUG(misc, 7, "  Executing First");
		vm.instruction = this->next;
	} else {
		DEBUG(misc, 7, "  Executing Last");
		vm.instruction = nullptr;
	}
}
/*virtual*/ void SignalSpecial::SetNext(SignalInstruction *next_insn)
{
	this->next = next_insn;
}

SignalIf::PseudoInstruction::PseudoInstruction(SignalProgram *prog, SignalOpcode op)
	: SignalInstruction(prog, op)
	{}

SignalIf::PseudoInstruction::PseudoInstruction(SignalProgram *prog, SignalIf *block, SignalOpcode op)
	: SignalInstruction(prog, op)
{
	this->block = block;
	if (op == PSO_IF_ELSE) {
		previous = block;
	} else if (op == PSO_IF_ENDIF) {
		previous = block->if_true;
	} else {
		NOT_REACHED();
	}
}

/*virtual*/ void SignalIf::PseudoInstruction::Remove()
{
	if (opcode == PSO_IF_ELSE) {
		this->block->if_true = nullptr;
		while(this->block->if_false) this->block->if_false->Remove();
	} else if (opcode == PSO_IF_ENDIF) {
		this->block->if_false = nullptr;
	} else {
		NOT_REACHED();
	}
	delete this;
}

/*virtual*/ void SignalIf::PseudoInstruction::Evaluate(SignalVM &vm)
{
	DEBUG(misc, 7, "  Executing If Pseudo Instruction %s", opcode == PSO_IF_ELSE ? "Else" : "Endif");
	vm.instruction = this->block->after;
}

/*virtual*/ void SignalIf::PseudoInstruction::SetNext(SignalInstruction *next_insn)
{
	if (this->opcode == PSO_IF_ELSE) {
		this->block->if_false = next_insn;
	} else if (this->opcode == PSO_IF_ENDIF) {
		this->block->after = next_insn;
	} else {
		NOT_REACHED();
	}
}

SignalIf::SignalIf(SignalProgram *prog, bool raw)
	: SignalInstruction(prog, PSO_IF)
{
	if (!raw) {
		this->condition = new SignalSimpleCondition(PSC_ALWAYS);
		this->if_true   = new PseudoInstruction(prog, this, PSO_IF_ELSE);
		this->if_false  = new PseudoInstruction(prog, this, PSO_IF_ENDIF);
		this->after     = nullptr;
	}
}

/*virtual*/ void SignalIf::Remove()
{
	delete this->condition;
	while (this->if_true)  this->if_true->Remove();

	this->previous->SetNext(this->after);
	this->after->SetPrevious(this->previous);
	delete this;
}

/*virtual*/ void SignalIf::Insert(SignalInstruction *before_insn)
{
	this->previous = before_insn->Previous();
	before_insn->Previous()->SetNext(this);
	before_insn->SetPrevious(this->if_false);
	this->after    = before_insn;
}

void SignalIf::SetCondition(SignalCondition *cond)
{
	assert(cond != this->condition);
	delete this->condition;
	this->condition = cond;
}

/*virtual*/ void SignalIf::Evaluate(SignalVM &vm)
{
	bool is_true = this->condition->Evaluate(vm);
	DEBUG(misc, 7, "  Executing If, taking %s branch", is_true ? "then" : "else");
	if (is_true) {
		vm.instruction = this->if_true;
	} else {
		vm.instruction = this->if_false;
	}
}

/*virtual*/ void SignalIf::SetNext(SignalInstruction *next_insn)
{
	this->if_true = next_insn;
}



SignalSet::SignalSet(SignalProgram *prog, SignalState state)
	: SignalInstruction(prog, PSO_SET_SIGNAL)
{
	this->to_state = state;
}

/*virtual*/ void SignalSet::Remove()
{
	this->next->SetPrevious(this->previous);
	this->previous->SetNext(this->next);
	delete this;
}

/*virtual*/ void SignalSet::Evaluate(SignalVM &vm)
{
	DEBUG(misc, 7, "  Executing SetSignal, making %s", this->to_state? "green" : "red");
	vm.state       = this->to_state;
	vm.instruction = nullptr;
}


/*virtual*/ void SignalSet::SetNext(SignalInstruction *next_insn)
{
	this->next = next_insn;
}

SignalProgram *GetExistingSignalProgram(SignalReference ref)
{
	ProgramList::iterator i = _signal_programs.find(ref);
	if (i != _signal_programs.end()) {
		assert(i->first == ref);
		return i->second;
	} else {
		return nullptr;
	}
}


SignalProgram *GetSignalProgram(SignalReference ref)
{
	SignalProgram *pr = GetExistingSignalProgram(ref);
	if (!pr) {
		pr = new SignalProgram(ref.tile, ref.track);
		_signal_programs[ref] = pr;
	} else {
		assert(pr->tile == ref.tile && pr->track == ref.track);
	}
	return pr;
}

void FreeSignalProgram(SignalReference ref)
{
	CloseWindowById(WC_SIGNAL_PROGRAM, (ref.tile << 3) | ref.track);
	ProgramList::iterator i = _signal_programs.find(ref);
	if (i != _signal_programs.end()) {
		delete i->second;
		_signal_programs.erase(i);
	}
}

void FreeSignalPrograms()
{
	_cleaning_signal_programs = true;
	ProgramList::iterator i, e;
	for (i = _signal_programs.begin(), e = _signal_programs.end(); i != e;) {
		delete i->second;
		// Must postincrement here to avoid iterator invalidation
		_signal_programs.erase(i++);
	}
	_cleaning_signal_programs = false;
}

SignalState RunSignalProgram(SignalReference ref, uint num_exits, uint num_green)
{
	SignalProgram *program = GetExistingSignalProgram(ref);
	if (program == nullptr) return SIGNAL_STATE_RED;
	SignalVM vm;
	vm.program = program;
	vm.num_exits = num_exits;
	vm.num_green = num_green;

	vm.instruction = program->first_instruction;
	vm.state = SIGNAL_STATE_RED;

	DEBUG(misc, 7, "%d exits, of which %d green", vm.num_exits, vm.num_green);
	vm.Execute();
	DEBUG(misc, 7, "Returning %s", vm.state == SIGNAL_STATE_GREEN ? "green" : "red");
	return vm.state;
}

void RemoveProgramDependencies(SignalReference dependency_target, SignalReference signal_to_update)
{
	SignalProgram *prog = GetExistingSignalProgram(signal_to_update);
	if (prog == nullptr) return;
	for (SignalInstruction *insn : prog->instructions) {
		if (insn->Opcode() == PSO_IF) {
			SignalIf *ifi = static_cast<SignalIf *>(insn);
			if (ifi->condition->ConditionCode() == PSC_SIGNAL_STATE) {
				SignalStateCondition* c = static_cast<SignalStateCondition*>(ifi->condition);
				if (c->sig_tile == dependency_target.tile && TrackdirToTrack(c->sig_track) == dependency_target.track) {
					c->Invalidate();
				}
			}
		}
	}

	InvalidateWindowData(WC_SIGNAL_PROGRAM, (signal_to_update.tile << 3) | signal_to_update.track);
	AddTrackToSignalBuffer(signal_to_update.tile, signal_to_update.track, GetTileOwner(signal_to_update.tile));
	UpdateSignalsInBuffer();
}

void RemoveProgramSlotDependencies(TraceRestrictSlotID slot_being_removed, SignalReference signal_to_update)
{
	SignalProgram *prog = GetExistingSignalProgram(signal_to_update);
	if (prog == nullptr) return;
	for (SignalInstruction *insn : prog->instructions) {
		if (insn->Opcode() == PSO_IF) {
			SignalIf *ifi = static_cast<SignalIf *>(insn);
			if (ifi->condition->ConditionCode() == PSC_SLOT_OCC || ifi->condition->ConditionCode() == PSC_SLOT_OCC_REM) {
				SignalSlotCondition *c = static_cast<SignalSlotCondition *>(ifi->condition);
				if (c->slot_id == slot_being_removed) {
					c->Invalidate();
				}
			}
		}
	}

	InvalidateWindowData(WC_SIGNAL_PROGRAM, (signal_to_update.tile << 3) | signal_to_update.track);
	AddTrackToSignalBuffer(signal_to_update.tile, signal_to_update.track, GetTileOwner(signal_to_update.tile));
	UpdateSignalsInBuffer();
}

void RemoveProgramCounterDependencies(TraceRestrictCounterID ctr_being_removed, SignalReference signal_to_update)
{
	SignalProgram *prog = GetExistingSignalProgram(signal_to_update);
	if (prog == nullptr) return;
	for (SignalInstruction *insn : prog->instructions) {
		if (insn->Opcode() == PSO_IF) {
			SignalIf *ifi = static_cast<SignalIf *>(insn);
			if (ifi->condition->ConditionCode() == PSC_COUNTER) {
				SignalCounterCondition *c = static_cast<SignalCounterCondition *>(ifi->condition);
				if (c->ctr_id == ctr_being_removed) {
					c->Invalidate();
				}
			}
		}
	}

	InvalidateWindowData(WC_SIGNAL_PROGRAM, (signal_to_update.tile << 3) | signal_to_update.track);
	AddTrackToSignalBuffer(signal_to_update.tile, signal_to_update.track, GetTileOwner(signal_to_update.tile));
	UpdateSignalsInBuffer();
}

void SignalProgram::DebugPrintProgram()
{
	DEBUG(misc, 5, "Program %p listing", this);
	for (size_t i = 0; i < this->instructions.size(); i++) {
		SignalInstruction *insn = this->instructions[i];
		DEBUG(misc, 5, " %d: Opcode %d, prev %d", int(i), int(insn->Opcode()),
					int(insn->Previous() ? insn->Previous()->Id() : -1));
	}
}

static CommandCost ValidateSignalTileTrack(TileIndex tile, Track track)
{
	if (!IsValidTrack(track)) return CMD_ERROR;

	if (!IsPlainRailTile(tile) || !HasTrack(tile, track) || !HasSignalOnTrack(tile, track) || !IsPresignalProgrammable(tile, track)) {
		return_cmd_error(STR_ERR_PROGSIG_NOT_THERE);
	}

	if (!IsTileOwner(tile, _current_company)) {
		return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER);
	}

	return CommandCost();
}

/** Insert a signal instruction into the signal program.
 *
 * @param tile The Tile on which to perform the operation
 * @param p1 Flags and information
 *   - Bits 0-2    Which track the signal sits on
 *   - Bits 3-18   ID of instruction to insert before
 *   - Bits 19-26  Which opcode to create
 *   - Bits 27-31  Reserved
 * @param p2 Depends upon instruction
 *   - PSO_SET_SIGNAL:
 *       - Colour to set the signal to
 * @param text unused
 */
CommandCost CmdInsertSignalInstruction(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Track track         = Extract<Track,        0,   3>(p1);
	uint instruction_id = GB(p1, 3, 16);
	SignalOpcode op     = Extract<SignalOpcode, 19,  8>(p1);

	CommandCost check_signal = ValidateSignalTileTrack(tile, track);
	if (check_signal.Failed()) return check_signal;

	SignalProgram *prog = GetSignalProgram(SignalReference(tile, track));
	if (prog == nullptr) {
		return_cmd_error(STR_ERR_PROGSIG_NOT_THERE);
	}
	if (instruction_id >= prog->instructions.size()) {
		return_cmd_error(STR_ERR_PROGSIG_INVALID_INSTRUCTION);
	}

	bool exec = (flags & DC_EXEC) != 0;

	SignalInstruction *insert_before = prog->instructions[instruction_id];
	switch (op) {
		case PSO_IF: {
			if (!exec) return CommandCost();
			SignalIf *if_ins = new SignalIf(prog);
			if_ins->Insert(insert_before);
			break;
		}

		case PSO_SET_SIGNAL: {
			SignalState ss = (SignalState) p2;
			if (ss > SIGNAL_STATE_MAX) return_cmd_error(STR_ERR_PROGSIG_INVALID_OPCODE);
			if (!exec) return CommandCost();

			SignalSet *set = new SignalSet(prog, ss);
			set->Insert(insert_before);
			break;
		}

		case PSO_FIRST:
		case PSO_LAST:
		case PSO_IF_ELSE:
		case PSO_IF_ENDIF:
		default:
			return_cmd_error(STR_ERR_PROGSIG_INVALID_OPCODE);
	}

	if (!exec) return CommandCost();
	AddTrackToSignalBuffer(tile, track, GetTileOwner(tile));
	UpdateSignalsInBuffer();
	InvalidateWindowData(WC_SIGNAL_PROGRAM, (tile << 3) | track);
	return CommandCost();
}

/** Modify a singal instruction
 *
 * @param tile The Tile on which to perform the operation
 * @param p1 Flags and information
 *   - Bits 0-2    Which track the signal sits on
 *   - Bits 3-18   ID of instruction to insert before
 * @param p2 Depends upon instruction
 *   - PSO_SET_SIGNAL:
 *       - Colour to set the signal to
 *   - PSO_IF:
 *       - Bit 0 If 0, set the condidion code:
 *         - Bit 1-8:  Conditon code to change to
 *       - Otherwise, if SignalVariableCondition:
 *        - Bits 1-2:  Which field to change (ConditionField)
 *        - Bits 3-31: Value to set field to
 *       - Otherwise, if SignalStateCondition:
 *        - Bits 1-4:  Trackdir on which signal is located
 *        - Bits 5-31: Tile on which signal is located
 * @param text unused
 */
CommandCost CmdModifySignalInstruction(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Track track         = Extract<Track, 0, 3 >(p1);
	uint instruction_id = GB(p1, 3, 16);

	CommandCost check_signal = ValidateSignalTileTrack(tile, track);
	if (check_signal.Failed()) return check_signal;

	SignalProgram *prog = GetExistingSignalProgram(SignalReference(tile, track));
	if (prog == nullptr) {
		return_cmd_error(STR_ERR_PROGSIG_NOT_THERE);
	}
	if (instruction_id >= prog->instructions.size()) {
		return_cmd_error(STR_ERR_PROGSIG_INVALID_INSTRUCTION);
	}

	bool exec = (flags & DC_EXEC) != 0;

	SignalInstruction *insn = prog->instructions[instruction_id];
	switch (insn->Opcode()) {
		case PSO_SET_SIGNAL: {
			SignalState state = (SignalState) p2;
			if (state > SIGNAL_STATE_MAX) {
				return_cmd_error(STR_ERR_PROGSIG_INVALID_SIGNAL_STATE);
			}
			if (!exec) return CommandCost();
			SignalSet *ss = static_cast<SignalSet*>(insn);
			ss->to_state = state;
		} break;

		case PSO_IF: {
			SignalIf *si = static_cast<SignalIf *>(insn);
			uint8_t act = GB(p2, 0, 1);
			if (act == 0) { // Set code
				SignalConditionCode code = (SignalConditionCode) GB(p2, 1, 8);
				if (code > PSC_MAX) {
					return_cmd_error(STR_ERR_PROGSIG_INVALID_CONDITION);
				}
				if (!exec) return CommandCost();

				SignalCondition *cond;
				switch (code) {
					case PSC_ALWAYS:
					case PSC_NEVER:
						cond = new SignalSimpleCondition(code);
						break;

					case PSC_NUM_GREEN:
					case PSC_NUM_RED:
						cond = new SignalVariableCondition(code);
						break;

					case PSC_SIGNAL_STATE:
						cond = new SignalStateCondition(SignalReference(tile, track), INVALID_TILE, INVALID_TRACKDIR);
						break;

					case PSC_SLOT_OCC:
					case PSC_SLOT_OCC_REM:
						cond = new SignalSlotCondition(code, SignalReference(tile, track), INVALID_TRACE_RESTRICT_SLOT_ID);
						break;

					case PSC_COUNTER:
						cond = new SignalCounterCondition(SignalReference(tile, track), INVALID_TRACE_RESTRICT_COUNTER_ID);
						break;

					default: NOT_REACHED();
				}
				si->SetCondition(cond);
			} else { // modify condition
				switch (si->condition->ConditionCode()) {
					case PSC_ALWAYS:
					case PSC_NEVER:
						return CommandCost(STR_ERR_PROGSIG_INVALID_CONDITION_FIELD);

					case PSC_NUM_GREEN:
					case PSC_NUM_RED: {
						SignalVariableCondition *vc = static_cast<SignalVariableCondition*>(si->condition);
						SignalConditionField f = (SignalConditionField) GB(p2, 1, 2);
						uint32_t val = GB(p2, 3, 27);
						if (f == SCF_COMPARATOR) {
							if (val > SGC_LAST) return_cmd_error(STR_ERR_PROGSIG_INVALID_COMPARATOR);
							if (!exec) return CommandCost();
							vc->comparator = (SignalComparator) val;
						} else if (f == SCF_VALUE) {
							if (!exec) return CommandCost();
							vc->value = val;
						} else {
							return_cmd_error(STR_ERR_PROGSIG_INVALID_CONDITION_FIELD);
						}
					} break;

					case PSC_SIGNAL_STATE: {
						SignalStateCondition *sc = static_cast<SignalStateCondition*>(si->condition);
						Trackdir  td = (Trackdir)  GB(p2, 1, 4);
						TileIndex ti = (TileIndex) GB(p2, 5, 27);

						if (!IsValidTile(ti) || !IsValidTrackdir(td) || !HasSignalOnTrackdir(ti, td)
								|| GetTileOwner(ti) != _current_company) {
							return_cmd_error(STR_ERR_PROGSIG_INVALID_SIGNAL);
						}
						if (!exec) return CommandCost();
						sc->SetSignal(ti, td);
					} break;

					case PSC_SLOT_OCC:
					case PSC_SLOT_OCC_REM: {
						SignalSlotCondition *sc = static_cast<SignalSlotCondition*>(si->condition);
						SignalConditionField f = (SignalConditionField) GB(p2, 1, 2);
						uint32_t val = GB(p2, 3, 27);
						if (f == SCF_COMPARATOR) {
							if (val > SGC_LAST) return_cmd_error(STR_ERR_PROGSIG_INVALID_COMPARATOR);
							if (!exec) return CommandCost();
							sc->comparator = (SignalComparator) val;
						} else if (f == SCF_VALUE) {
							if (!exec) return CommandCost();
							sc->value = val;
						} else if (f == SCF_SLOT_COUNTER) {
							if (val != INVALID_TRACE_RESTRICT_SLOT_ID && !TraceRestrictSlot::IsValidID(val)) return CMD_ERROR;
							if (!exec) return CommandCost();
							sc->SetSlot((TraceRestrictSlotID) val);
						} else {
							return_cmd_error(STR_ERR_PROGSIG_INVALID_CONDITION_FIELD);
						}
					} break;

					case PSC_COUNTER: {
						SignalCounterCondition *sc = static_cast<SignalCounterCondition *>(si->condition);
						SignalConditionField f = (SignalConditionField) GB(p2, 1, 2);
						uint32_t val = GB(p2, 3, 27);
						if (f == SCF_COMPARATOR) {
							if (val > SGC_LAST) return_cmd_error(STR_ERR_PROGSIG_INVALID_COMPARATOR);
							if (!exec) return CommandCost();
							sc->comparator = (SignalComparator) val;
						} else if (f == SCF_VALUE) {
							if (!exec) return CommandCost();
							sc->value = val;
						} else if (f == SCF_SLOT_COUNTER) {
							if (val != INVALID_TRACE_RESTRICT_COUNTER_ID && !TraceRestrictCounter::IsValidID(val)) return CMD_ERROR;
							if (!exec) return CommandCost();
							sc->SetCounter((TraceRestrictCounterID) val);
						} else {
							return_cmd_error(STR_ERR_PROGSIG_INVALID_CONDITION_FIELD);
						}
					} break;
				}
			}
		} break;

		case PSO_FIRST:
		case PSO_LAST:
		case PSO_IF_ELSE:
		case PSO_IF_ENDIF:
		default:
			return CommandCost(STR_ERR_PROGSIG_INVALID_OPCODE);
	}

	if (!exec) return CommandCost();

	AddTrackToSignalBuffer(tile, track, GetTileOwner(tile));
	UpdateSignalsInBuffer();
	InvalidateWindowData(WC_SIGNAL_PROGRAM, (tile << 3) | track);
	return CommandCost();
}

/** Remove an instruction from a signal program
 *
 * @param tile The Tile on which to perform the operation
 * @param p1 Flags and information
 *   - Bits 0-2    Which track the signal sits on
 *   - Bits 3-18   ID of instruction to insert before
 * @param p2 unused
 * @param text unused
 */
CommandCost CmdRemoveSignalInstruction(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Track track         = Extract<Track, 0, 3 >(p1);
	uint instruction_id = GB(p1, 3, 16);

	CommandCost check_signal = ValidateSignalTileTrack(tile, track);
	if (check_signal.Failed()) return check_signal;

	SignalProgram *prog = GetExistingSignalProgram(SignalReference(tile, track));
	if (prog == nullptr) {
		return_cmd_error(STR_ERR_PROGSIG_NOT_THERE);
	}

	if (instruction_id >= prog->instructions.size()) {
		return_cmd_error(STR_ERR_PROGSIG_INVALID_INSTRUCTION);
	}

	bool exec = (flags & DC_EXEC) != 0;

	SignalInstruction *insn = prog->instructions[instruction_id];
	switch (insn->Opcode()) {
		case PSO_SET_SIGNAL:
		case PSO_IF:
			if (!exec) return CommandCost();
			insn->Remove();
			break;

		case PSO_FIRST:
		case PSO_LAST:
		case PSO_IF_ELSE:
		case PSO_IF_ENDIF:
		default:
			return_cmd_error(STR_ERR_PROGSIG_INVALID_OPCODE);
	}

	if (!exec) return CommandCost();
	AddTrackToSignalBuffer(tile, track, GetTileOwner(tile));
	UpdateSignalsInBuffer();
	InvalidateWindowData(WC_SIGNAL_PROGRAM, (tile << 3) | track);
	return CommandCost();
}

static void CloneInstructions(SignalProgram *prog, SignalInstruction *insert_before, SignalInstruction *si)
{
	while (si != nullptr) {
		switch (si->Opcode()) {
			case PSO_SET_SIGNAL: {
				SignalSet *set = new SignalSet(prog, ((SignalSet *)si)->to_state);
				set->Insert(insert_before);

				si = ((SignalSet *)si)->next;
				break;
			}

			case PSO_IF: {
				SignalIf *if_ins = new SignalIf(prog);
				if_ins->Insert(insert_before);

				CloneInstructions(prog, if_ins->if_true, ((SignalIf *)si)->if_true);
				CloneInstructions(prog, if_ins->if_false, ((SignalIf *)si)->if_false);

				SignalCondition *src_cond = ((SignalIf *) si)->condition;
				SignalConditionCode code = src_cond->ConditionCode();
				switch (code) {
					case PSC_ALWAYS:
					case PSC_NEVER:
						if_ins->SetCondition(new SignalSimpleCondition(code));
						break;

					case PSC_NUM_GREEN:
					case PSC_NUM_RED: {
						SignalVariableCondition *cond = new SignalVariableCondition(code);
						cond->comparator = ((SignalVariableCondition *) src_cond)->comparator;
						cond->value = ((SignalVariableCondition *) src_cond)->value;
						if_ins->SetCondition(cond);
						break;
					}

					case PSC_SIGNAL_STATE: {
						SignalStateCondition *src = ((SignalStateCondition *) src_cond);
						if_ins->SetCondition(new SignalStateCondition(SignalReference(prog->tile, prog->track), src->sig_tile, src->sig_track));
						break;
					}

					case PSC_SLOT_OCC:
					case PSC_SLOT_OCC_REM: {
						SignalSlotCondition *src = ((SignalSlotCondition *) src_cond);
						SignalSlotCondition *cond = new SignalSlotCondition(code, SignalReference(prog->tile, prog->track), src->slot_id);
						cond->comparator = src->comparator;
						cond->value = src->value;
						if_ins->SetCondition(cond);
						break;
					}

					case PSC_COUNTER: {
						SignalCounterCondition *src = ((SignalCounterCondition *) src_cond);
						SignalCounterCondition *cond = new SignalCounterCondition(SignalReference(prog->tile, prog->track), src->ctr_id);
						cond->comparator = src->comparator;
						cond->value = src->value;
						if_ins->SetCondition(cond);
						break;
					}

					default: NOT_REACHED();
				}

				si = ((SignalIf *)si)->after;
				break;
			}

			case PSO_LAST:
			case PSO_IF_ELSE:
			case PSO_IF_ENDIF:
				return;

			default:
				NOT_REACHED();
		}
	}
}

/** Insert a signal instruction into the signal program.
 *
 * @param tile The Tile on which to perform the operation
 * @param p1 Flags and information
 *   - Bits 0-2    Which track the signal sits on
 *   - Bits 3-6    Management code
 *   For clone action:
 *   - Bits 7-9    Which track the clone source signal sits on
 * @param p2
 *   For clone action:
 *   - Tile of clone source signal
 * @param text unused
 */
CommandCost CmdSignalProgramMgmt(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	bool exec = (flags & DC_EXEC) != 0;

	Track track = Extract<Track, 0, 3>(p1);
	SignalProgramMgmtCode mgmt = static_cast<SignalProgramMgmtCode>(GB(p1, 3, 4));

	CommandCost check_signal = ValidateSignalTileTrack(tile, track);
	if (check_signal.Failed()) return check_signal;

	switch (mgmt) {
		case SPMC_REMOVE: {
			SignalProgram *prog = GetExistingSignalProgram(SignalReference(tile, track));
			if (prog == nullptr) return_cmd_error(STR_ERR_PROGSIG_NOT_THERE);
			if (exec) {
				prog->first_instruction->Remove();
			}
			break;
		}

		case SPMC_CLONE: {
			SignalProgram *prog = GetSignalProgram(SignalReference(tile, track));

			TileIndex src_tile = p2;
			Track src_track = Extract<Track, 7, 3>(p1);
			if (!IsValidTrack(src_track) || !IsPlainRailTile(src_tile) || !HasTrack(src_tile, src_track)) {
				return CMD_ERROR;
			}

			if (!IsTileOwner(src_tile, _current_company)) return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER);

			SignalProgram *src_prog = GetExistingSignalProgram(SignalReference(src_tile, src_track));
			if (!src_prog) return_cmd_error(STR_ERR_PROGSIG_NOT_THERE);

			if (exec) {
				prog->first_instruction->Remove();
				CloneInstructions(prog, prog->last_instruction, ((SignalSpecial *) src_prog->first_instruction)->next);
			}
			break;
		}

		default:
			return CMD_ERROR;
	}
	if (exec) {
		AddTrackToSignalBuffer(tile, track, GetTileOwner(tile));
		UpdateSignalsInBuffer();
		InvalidateWindowData(WC_SIGNAL_PROGRAM, (tile << 3) | track);
	}
	return CommandCost();
}
