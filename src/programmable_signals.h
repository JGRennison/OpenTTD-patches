/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file programmable_signals.h Programmable Pre-Signals */

#ifndef PROGRAMMABLE_SIGNALS_H
#define PROGRAMMABLE_SIGNALS_H
#include "rail_map.h"
#include "tracerestrict.h"
#include "core/container_func.hpp"
#include <map>
#include <vector>

/** @defgroup progsigs Programmable Pre-Signals */
///@{

/** The Programmable Pre-Signal virtual machine.
 *
 * This structure contains the state of the currently executing signal program.
 */
struct SignalVM;

class SignalInstruction;
class SignalSpecial;
typedef std::vector<SignalInstruction*> InstructionList;

enum SignalProgramMgmtCode {
	SPMC_REMOVE,      ///< Remove program
	SPMC_CLONE,       ///< Clone program
};

/** The actual programmable pre-signal information */
struct SignalProgram {
	SignalProgram(TileIndex tile, Track track, bool raw = false);
	~SignalProgram();
	void DebugPrintProgram();

	TileIndex tile;
	Track track;

	SignalSpecial *first_instruction;
	SignalSpecial *last_instruction;
	InstructionList instructions;
};

/** Programmable Pre-Signal opcode.
 *
 * Opcode types are discriminated by this enumeration. It is primarily used for
 * code which must be able to inspect the type of a signal operation, rather than
 * evaluate it (such as the programming GUI)
 */
enum SignalOpcode {
	PSO_FIRST      = 0,     ///< Start pseudo instruction
	PSO_LAST       = 1,     ///< End pseudo instruction
	PSO_IF         = 2,     ///< If instruction
	PSO_IF_ELSE    = 3,     ///< If Else pseudo instruction
	PSO_IF_ENDIF   = 4,     ///< If Endif pseudo instruction
	PSO_SET_SIGNAL = 5,     ///< Set signal instruction

	PSO_END,
	PSO_INVALID   = 0xFF
};
template <> struct EnumPropsT<SignalOpcode> : MakeEnumPropsT<SignalOpcode, uint8_t, PSO_FIRST, PSO_END, PSO_INVALID, 8> {};

/** Signal instruction base class. All instructions must derive from this. */
class SignalInstruction {
public:
	/// Get the instruction's opcode
	inline SignalOpcode Opcode() const { return this->opcode; }

	/// Get the previous instruction. If this is nullptr, then this is the first
	/// instruction.
	inline SignalInstruction *Previous() const { return this->previous; }

	/// Get the Id of this instruction
	inline int Id() const
	// Const cast is safe (perculiarity of SmallVector)
	{ return find_index(program->instructions, const_cast<SignalInstruction*>(this)); }

	/// Insert this instruction, placing it before @p before_insn
	virtual void Insert(SignalInstruction *before_insn);

	/// Evaluate the instruction. The instruction should update the VM state.
	virtual void Evaluate(SignalVM &vm) = 0;

	/// Remove the instruction. When removing itself, an instruction should
	/// <ul>
	///   <li>Set next->previous to previous
	///   <li>Set previous->next to next
	///   <li>Destroy any other children
	/// </ul>
	virtual void Remove() = 0;

	/// Gets a reference to the previous member. This is only intended for use by
	/// the saveload code.
	inline SignalInstruction *&GetPrevHandle()
	{ return previous; }

	/// Sets the previous instruction of this instruction. This is only intended
	/// to be used by instructions to update links during insertion and removal.
	inline void SetPrevious(SignalInstruction *prev)
	{ previous = prev; }
	/// Set the next instruction. This is only intended to be used by instructions
	/// to update links during insertion and removal
	virtual void SetNext(SignalInstruction *next_insn) = 0;

protected:
	/// Constructs an instruction
	/// @param prog the program to add this instruction to
	/// @param op the opcode of the instruction
	SignalInstruction(SignalProgram *prog, SignalOpcode op) ;
	virtual ~SignalInstruction();

	const SignalOpcode opcode;
	SignalInstruction *previous;
	SignalProgram *program;
};

/** Programmable Pre-Signal condition code.
 *
 * These discriminate conditions in much the same way that SignalOpcode
 * discriminates instructions.
 */
enum SignalConditionCode {
	PSC_ALWAYS = 0,       ///< Always true
	PSC_NEVER = 1,        ///< Always false
	PSC_NUM_GREEN = 2,    ///< Number of green signals behind this signal
	PSC_NUM_RED = 3,      ///< Number of red signals behind this signal
	PSC_SIGNAL_STATE = 4, ///< State of another signal
	PSC_SLOT_OCC = 5,     ///< Slot occupancy
	PSC_SLOT_OCC_REM = 6, ///< Slot occupancy remaining
	PSC_COUNTER = 7,      ///< Counter value

	PSC_MAX = PSC_COUNTER
};

class SignalCondition {
public:
	/// Get the condition's code
	inline SignalConditionCode ConditionCode() const { return this->cond_code; }

	/// Evaluate the condition
	virtual bool Evaluate(SignalVM& vm) = 0;

	/// Destroy the condition. Any children should also be destroyed
	virtual ~SignalCondition();

protected:
	SignalCondition(SignalConditionCode code) : cond_code(code) {}

	const SignalConditionCode cond_code;
};

// -- Condition codes --
/** Simple condition code. These conditions have no complex inputs, and can be
 *  evaluated directly from VM state and their condition code.
 */
class SignalSimpleCondition: public SignalCondition {
public:
	SignalSimpleCondition(SignalConditionCode code);
	bool Evaluate(SignalVM& vm) override;
};

/** Comparator to use for variable conditions. */
enum SignalComparator {
	SGC_EQUALS = 0,            ///< the variable is equal to the specified value
	SGC_NOT_EQUALS = 1,        ///< the variable is not equal to the specified value
	SGC_LESS_THAN = 2,         ///< the variable is less than specified value
	SGC_LESS_THAN_EQUALS = 3,  ///< the variable is less than or equal to the specified value
	SGC_MORE_THAN = 4,         ///< the variable is greater than the specified value
	SGC_MORE_THAN_EQUALS = 5,  ///< the variable is grater than or equal to the specified value
	SGC_IS_TRUE = 6,           ///< the variable is true (non-zero)
	SGC_IS_FALSE = 7,          ///< the variable is false (zero)

	SGC_LAST = SGC_IS_FALSE
};

/** Which field to modify in a condition. A parameter to CMD_MODIFY_SIGNAL_INSTRUCTION */
enum SignalConditionField {
	SCF_COMPARATOR = 0,       ///< the comparator (value from SignalComparator enum)
	SCF_VALUE = 1,            ///< the value (integer value)
	SCF_SLOT_COUNTER = 2,     ///< the slot or counter
};

class SignalConditionComparable: public SignalCondition {
protected:
	bool EvaluateComparable(uint32_t var_val);

public:
	SignalConditionComparable(SignalConditionCode code) : SignalCondition(code) {}
	SignalComparator comparator;
	uint32_t value;
};

/** A conditon based upon comparing a variable and a value. This condition can be
 *  considered similar to the conditonal jumps in vehicle orders.
 *
 * The variable is specified by the conditon code, the comparison by @p comparator, and
 * the value to compare against by @p value. The condition returns the result of that value.
 */
class SignalVariableCondition: public SignalConditionComparable {
public:
	/// Constructs a condition refering to the value @p code refers to. Sets the
	/// comparator and value to sane defaults.
	SignalVariableCondition(SignalConditionCode code);

	/// Evaluates the condition
	bool Evaluate(SignalVM &vm) override;
};

/** A condition which is based upon the state of another signal. */
class SignalStateCondition: public SignalCondition {
	public:
		SignalStateCondition(SignalReference this_sig, TileIndex sig_tile, Trackdir sig_track);

		void SetSignal(TileIndex tile, Trackdir track);
		bool IsSignalValid() const;
		bool CheckSignalValid();
		void Invalidate();

		bool Evaluate(SignalVM& vm) override;
		virtual ~SignalStateCondition();

		SignalReference this_sig;
		TileIndex sig_tile;
		Trackdir sig_track;
};

/** A condition which is based upon the value of a slot. */
class SignalSlotCondition: public SignalConditionComparable {
	public:
		SignalSlotCondition(SignalConditionCode code, SignalReference this_sig, TraceRestrictSlotID slot_id);

		void SetSlot(TraceRestrictSlotID slot_id);
		bool IsSlotValid() const;
		bool CheckSlotValid();
		void Invalidate();

		bool Evaluate(SignalVM& vm) override;
		virtual ~SignalSlotCondition();

		SignalReference this_sig;
		TraceRestrictSlotID slot_id;
};

/** A condition which is based upon the value of a counter. */
class SignalCounterCondition: public SignalConditionComparable {
	public:
		SignalCounterCondition(SignalReference this_sig, TraceRestrictCounterID ctr_id);

		void SetCounter(TraceRestrictCounterID ctr_id);
		bool IsCounterValid() const;
		bool CheckCounterValid();
		void Invalidate();

		bool Evaluate(SignalVM& vm) override;
		virtual ~SignalCounterCondition();

		SignalReference this_sig;
		TraceRestrictCounterID ctr_id;
};

// -- Instructions

/** The special start and end pseudo instructions.
 *
 * These instructions serve two purposes:
 * <ol>
 *   <li>They permit every other instruction to assume that there is another
 *       following it. This makes the code much simpler (and by extension less
 *       error prone)</li>
 *   <li>Particularly in the case of the End instruction, they provide an
 *       instruction in the user interface that can be clicked on to add
 *       instructions at the end of a program</li>
 * </ol>
 */
class SignalSpecial: public SignalInstruction {
public:
	/** Constructs a special signal of the opcode @p op in program @p prog.
	 *
	 * Generally you should not need to call this; it will be called by the
	 * program's constructor. An exception is in the saveload code, which needs
	 * to construct raw objects to deserialize into
	 */
	SignalSpecial(SignalProgram *prog, SignalOpcode op);

	/** Evaluates the instruction. If this is an Start instruction, flow will be
	 * vectored to the first instruction; if it is an End instruction, the program
	 * will terminate and the signal will be left red.
	 */
	void Evaluate(SignalVM &vm) override;

	/** Links the first and last instructions in the program. Generally only to be
	 * called from the SignalProgram constructor.
	 */
	static void link(SignalSpecial *first, SignalSpecial *last);

	/** Removes this instruction. If this is the start instruction, then all of
	 * the other instructions in the program will be successively removed,
	 * (emptying it). If this is the End instruction, then it will do nothing.
	 *
	 * This operation, unlike when executed on most instructions, does not destroy
	 * the instruction.
	 */
	void Remove() override;

	/** The next instruction after this one. On the End instruction, this should
	* be nullptr.
	*/
	SignalInstruction *next;

	void SetNext(SignalInstruction *next_insn) override;
};

/** If signal instruction. This is perhaps the most important, as without it,
 *  programmable pre-signals are pretty useless.
 *
 * It's also the most complex!
 */
class SignalIf: public SignalInstruction {
public:
	/** The If-Else and If-Endif pseudo instructions. The Else instruction
	 * follows the Then block, and the Endif instruction follows the Else block.
	 *
	 * These serve two purposes:
	 * <ul>
	 *  <li>They correctly vector the execution to after the if block
	 *      (if needed)
	 *  <li>They provide an instruction for the GUI to insert other instructions
	 *      before.
	 * </ul>
	 */
	class PseudoInstruction: public SignalInstruction {
	public:
		/** Normal constructor. The pseudo instruction will be constructed as
		 * belonging to @p block.
		 */
		PseudoInstruction(SignalProgram *prog, SignalIf *block, SignalOpcode op);

		/** Constructs an empty instruction of type @p op. This should only be used
		 *  by the saveload code during deserialization. The instruction must have
		 * its block field set correctly before the program is run.
		 */
		PseudoInstruction(SignalProgram *prog, SignalOpcode op);

		/** Removes the pseudo instruction. Unless you are also removing the If it
		 * belongs to, this is nonsense and dangerous.
		 */
		void Remove() override;

		/** Evaluate the pseudo instruction. This involves vectoring execution to
		 * the instruction after the if.
		 */
		void Evaluate(SignalVM &vm) override;

		/** The block to which this instruction belongs */
		SignalIf *block;
		void SetNext(SignalInstruction *next_insn) override;
	};

public:
	/** Constructs an If instruction belonging to program @p prog. If @p raw is
	 * true, then the instruction is constructed raw (in order for the
	 * deserializer to be able to correctly deserialize the instruction).
	 */
	SignalIf(SignalProgram *prog, bool raw = false);

	/** Sets the instruction's condition, and releases the old condition */
	void SetCondition(SignalCondition *cond);

	/** Evaluates the If and takes the appropriate branch */
	void Evaluate(SignalVM &vm) override;

	void Insert(SignalInstruction *before_insn) override;

	/** Removes the If and all of its children */
	void Remove() override;

	SignalCondition *condition;    ///< The if conditon
	SignalInstruction *if_true;    ///< The branch to take if true
	SignalInstruction *if_false;   ///< The branch to take if false
	SignalInstruction *after;      ///< The branch to take after the If

	void SetNext(SignalInstruction *next_insn) override;
};

/** Set signal instruction. This sets the state of the signal and terminates execution */
class SignalSet: public SignalInstruction {
public:
	/// Constructs the instruction and sets the state the signal is to be set to
	SignalSet(SignalProgram *prog, SignalState = SIGNAL_STATE_RED);

	void Evaluate(SignalVM &vm) override;
	void Remove() override;

	/// The state to set the signal to
	SignalState to_state;

	/// The instruction following this one (for the editor)
	SignalInstruction *next;

	void SetNext(SignalInstruction *next_insn) override;
};

/// The map type used for looking up signal programs
typedef std::map<SignalReference, SignalProgram*> ProgramList;

/// The global signal program list
extern ProgramList _signal_programs;

/// Verifies that a SignalReference refers to a signal which has a program.
inline bool HasProgrammableSignals(SignalReference ref)
{
	return IsTileType(ref.tile, MP_RAILWAY) && GetRailTileType(ref.tile) == RAIL_TILE_SIGNALS
	    && IsPresignalProgrammable(ref.tile, ref.track);
}

/// Shows the programming window for the signal identified by @p tile and
/// @p track.
void ShowSignalProgramWindow(SignalReference ref);

/// Gets the signal program for the tile identified by @p t and @p track.
/// An empty program will be constructed if none is specified
SignalProgram *GetSignalProgram(SignalReference ref);

SignalProgram *GetExistingSignalProgram(SignalReference ref);

/// Frees a signal program by tile and track
void FreeSignalProgram(SignalReference ref);

/// Frees all signal programs (For use when creating a new game)
void FreeSignalPrograms();

/// Runs the signal program, specifying the following parameters.
SignalState RunSignalProgram(SignalReference ref, uint num_exits, uint num_green);

/// Remove dependencies on signal @p on from @p by
void RemoveProgramDependencies(SignalReference dependency_target, SignalReference signal_to_update);
///@}

#endif
