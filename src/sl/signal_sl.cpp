/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file signal_sl.cpp Code handling saving and loading of signals */

#include "../stdafx.h"
#include "../programmable_signals.h"
#include "../core/alloc_type.hpp"
#include "../core/bitmath_func.hpp"
#include <vector>
#include "saveload.h"
#include "saveload_buffer.h"

typedef std::vector<uint8_t> Buffer;

// Variable length integers are stored in Variable Length Quantity
// format (http://en.wikipedia.org/wiki/Variable-length_quantity)

static void WriteVLI(Buffer &b, uint i)
{
	uint lsmask =  0x7F;
	uint msmask = ~0x7F;
	while(i & msmask) {
		uint8_t part = (i & lsmask) | 0x80;
		b.push_back(part);
		i >>= 7;
	}
	b.push_back((uint8_t) i);
}

static uint ReadVLI()
{
	uint shift = 0;
	uint val = 0;
	uint8_t b;

	b = SlReadByte();
	while(b & 0x80) {
		val |= uint(b & 0x7F) << shift;
		shift += 7;
		b = SlReadByte();
	}
	val |= uint(b) << shift;
	return val;
}

static void WriteCondition(Buffer &b, SignalCondition *c)
{
	WriteVLI(b, c->ConditionCode());
	switch(c->ConditionCode()) {
		case PSC_NUM_GREEN:
		case PSC_NUM_RED: {
			SignalVariableCondition *vc = static_cast<SignalVariableCondition*>(c);
			WriteVLI(b, vc->comparator);
			WriteVLI(b, vc->value);
		} break;

		case PSC_SIGNAL_STATE: {
			SignalStateCondition *sc = static_cast<SignalStateCondition*>(c);
			WriteVLI(b, sc->sig_tile);
			WriteVLI(b, sc->sig_track);
		} break;

		case PSC_SLOT_OCC:
		case PSC_SLOT_OCC_REM: {
			SignalSlotCondition *cc = static_cast<SignalSlotCondition*>(c);
			WriteVLI(b, cc->slot_id);
			WriteVLI(b, cc->comparator);
			WriteVLI(b, cc->value);
		} break;

		case PSC_COUNTER: {
			SignalCounterCondition *cc = static_cast<SignalCounterCondition*>(c);
			WriteVLI(b, cc->ctr_id);
			WriteVLI(b, cc->comparator);
			WriteVLI(b, cc->value);
		} break;

		default:
			break;
	}
}

static SignalCondition *ReadCondition(SignalReference this_sig)
{
	SignalConditionCode code = (SignalConditionCode) ReadVLI();
	switch(code) {
		case PSC_NUM_GREEN:
		case PSC_NUM_RED: {
			SignalVariableCondition *c = new SignalVariableCondition(code);
			c->comparator = (SignalComparator) ReadVLI();
			if(c->comparator > SGC_LAST) NOT_REACHED();
			c->value = ReadVLI();
			return c;
		}

		case PSC_SIGNAL_STATE: {
			TileIndex ti = (TileIndex) ReadVLI();
			Trackdir  td = (Trackdir) ReadVLI();
			return new SignalStateCondition(this_sig, ti, td);
		}

		case PSC_SLOT_OCC:
		case PSC_SLOT_OCC_REM: {
			TraceRestrictSlotID slot_id = (TraceRestrictSlotID) ReadVLI();
			SignalSlotCondition *c = new SignalSlotCondition(code, this_sig, slot_id);
			c->comparator = (SignalComparator) ReadVLI();
			if(c->comparator > SGC_LAST) NOT_REACHED();
			c->value = ReadVLI();
			return c;
		} break;

		case PSC_COUNTER: {
			TraceRestrictCounterID ctr_id = (TraceRestrictCounterID) ReadVLI();
			SignalCounterCondition *c = new SignalCounterCondition(this_sig, ctr_id);
			c->comparator = (SignalComparator) ReadVLI();
			if(c->comparator > SGC_LAST) NOT_REACHED();
			c->value = ReadVLI();
			return c;
		}

		default:
			return new SignalSimpleCondition(code);
	}
}

static void Save_SPRG()
{
	// Check for, and dispose of, any signal information on a tile which doesn't have signals.
	// This indicates that someone removed the signals from the tile but didn't clean them up.
	// (This code is to detect bugs and limit their consquences, not to cover them up!)
	for(ProgramList::iterator i = _signal_programs.begin(), e = _signal_programs.end();
			i != e; ++i) {
		SignalReference ref = i->first;
		if(!HasProgrammableSignals(ref)) {
			DEBUG(sl, 0, "Programmable pre-signal information for (%x, %d) has been leaked!",
						ref.tile, ref.track);
			++i;
			FreeSignalProgram(ref);
			if(i == e) break;
		}
	}

	// OK, we can now write out our programs
	Buffer b;
	WriteVLI(b, (uint)_signal_programs.size());
	for(ProgramList::iterator i = _signal_programs.begin(), e = _signal_programs.end();
			i != e; ++i) {
		SignalProgram *prog = i->second;

		WriteVLI(b, prog->tile);
		WriteVLI(b, prog->track);
		WriteVLI(b, (uint)prog->instructions.size());
		for (SignalInstruction *insn : prog->instructions) {
			WriteVLI(b, insn->Opcode());
			if(insn->Opcode() != PSO_FIRST)
				WriteVLI(b, insn->Previous()->Id());
			switch(insn->Opcode()) {
				case PSO_FIRST: {
					SignalSpecial *s = static_cast<SignalSpecial*>(insn);
					WriteVLI(b, s->next->Id());
					break;
				}

				case PSO_LAST: break;

				case PSO_IF: {
					SignalIf *i = static_cast<SignalIf*>(insn);
					WriteCondition(b, i->condition);
					WriteVLI(b, i->if_true->Id());
					WriteVLI(b, i->if_false->Id());
					WriteVLI(b, i->after->Id());
					break;
				}

				case PSO_IF_ELSE:
				case PSO_IF_ENDIF: {
					SignalIf::PseudoInstruction *p = static_cast<SignalIf::PseudoInstruction*>(insn);
					WriteVLI(b, p->block->Id());
					break;
				}

				case PSO_SET_SIGNAL: {
					SignalSet *s = static_cast<SignalSet*>(insn);
					WriteVLI(b, s->next->Id());
					WriteVLI(b, s->to_state ? 1 : 0);
					break;
				}

				default: NOT_REACHED();
			}
		}
	}

	uint size = (uint)b.size();
	SlSetLength(size);
	MemoryDumper::GetCurrent()->CopyBytes(b.data(), size);
}

// We don't know the pointer values that need to be stored in various
// instruction fields at load time, so we need to instead store the IDs and
// then fix them up once all of the instructions have been loaded.
//
// Additionally, we store the opcode type we expect (if we expect a specific one)
// to check for consistency (For example, an If Pseudo Instruction's block should
// point at an If!)
struct Fixup {
	Fixup(SignalInstruction **p, SignalOpcode type)
		: type(type), ptr(p)
	{}

	SignalOpcode type;
	SignalInstruction **ptr;
};

typedef std::vector<Fixup> FixupList;

template<typename T>
static void MakeFixup(FixupList &l, T *&ir, uint id, SignalOpcode op = PSO_INVALID)
{
	ir = reinterpret_cast<T*>((size_t)id);
	l.emplace_back(reinterpret_cast<SignalInstruction**>(&ir), op);
}

static void DoFixups(FixupList &l, InstructionList &il)
{
	for (Fixup &i : l) {
		uint id = (uint)reinterpret_cast<size_t>(*(i.ptr));
		if (id >= il.size())
			NOT_REACHED();

		*(i.ptr) = il[id];

		if (i.type != PSO_INVALID && (*(i.ptr))->Opcode() != i.type) {
			DEBUG(sl, 0, "Expected Id %d to be %d, but was in fact %d", id, i.type, (*(i.ptr))->Opcode());
			NOT_REACHED();
		}
	}
}

static void Load_SPRG()
{
	uint count = ReadVLI();
	for(uint i = 0; i < count; i++) {
		FixupList l;
		TileIndex tile    = ReadVLI();
		Track     track   = (Track) ReadVLI();
		uint instructions = ReadVLI();
		SignalReference ref(tile, track);

		SignalProgram *sp = new SignalProgram(tile, track, true);
		_signal_programs[ref] = sp;

		for(uint j = 0; j < instructions; j++) {
			SignalOpcode op = (SignalOpcode) ReadVLI();
			switch(op) {
				case PSO_FIRST: {
					sp->first_instruction = new SignalSpecial(sp, PSO_FIRST);
					sp->first_instruction->GetPrevHandle() = nullptr;
					MakeFixup(l, sp->first_instruction->next, ReadVLI());
					break;
				}

				case PSO_LAST: {
					sp->last_instruction = new SignalSpecial(sp, PSO_LAST);
					sp->last_instruction->next = nullptr;
					MakeFixup(l, sp->last_instruction->GetPrevHandle(), ReadVLI());
					break;
				}

				case PSO_IF: {
					SignalIf *i = new SignalIf(sp, true);
					MakeFixup(l, i->GetPrevHandle(), ReadVLI());
					i->condition = ReadCondition(ref);
					MakeFixup(l, i->if_true,  ReadVLI());
					MakeFixup(l, i->if_false, ReadVLI());
					MakeFixup(l, i->after, ReadVLI());
					break;
				}

				case PSO_IF_ELSE:
				case PSO_IF_ENDIF: {
					SignalIf::PseudoInstruction *p = new SignalIf::PseudoInstruction(sp, op);
					MakeFixup(l, p->GetPrevHandle(), ReadVLI());
					MakeFixup(l, p->block, ReadVLI(), PSO_IF);
					break;
				}

				case PSO_SET_SIGNAL: {
					SignalSet *s = new SignalSet(sp);
					MakeFixup(l, s->GetPrevHandle(), ReadVLI());
					MakeFixup(l, s->next, ReadVLI());
					s->to_state = (SignalState) ReadVLI();
					if(s->to_state > SIGNAL_STATE_MAX) NOT_REACHED();
					break;
				}

				default: NOT_REACHED();
			}
		}

		DoFixups(l, sp->instructions);
	}
}

extern const ChunkHandler signal_chunk_handlers[] = {
	{ 'SPRG', Save_SPRG, Load_SPRG, nullptr, nullptr, CH_RIFF },
};

extern const ChunkHandlerTable _signal_chunk_handlers(signal_chunk_handlers);
