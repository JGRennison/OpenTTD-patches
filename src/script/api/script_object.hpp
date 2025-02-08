/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_object.hpp Main object, on which all objects depend. */

#ifndef SCRIPT_OBJECT_HPP
#define SCRIPT_OBJECT_HPP

#include "../../command_type.h"
#include "../../company_type.h"
#include "../../road_type.h"
#include "../../rail_type.h"
#include "../../core/random_func.hpp"

#include "script_types.hpp"
#include "script_log_types.hpp"
#include "../script_suspend.hpp"
#include "../squirrel.hpp"

#include <utility>

/**
 * The callback function for Mode-classes.
 */
typedef bool (ScriptModeProc)();

/**
 * The callback function for Async Mode-classes.
 */
typedef bool (ScriptAsyncModeProc)();

/**
 * Simple counted object. Use it as base of your struct/class if you want to use
 *  basic reference counting. Your struct/class will destroy and free itself when
 *  last reference to it is released (using Release() method). The initial reference
 *  count (when it is created) is zero (don't forget AddRef() at least one time if
 *  not using ScriptObjectRef.
 * @api -all
 */
class SimpleCountedObject {
public:
	SimpleCountedObject() : ref_count(0) {}
	virtual ~SimpleCountedObject() = default;

	inline void AddRef() { ++this->ref_count; }
	void Release();
	virtual void FinalRelease() {};

private:
	int32_t ref_count;
};

/**
 * Uper-parent object of all API classes. You should never use this class in
 *   your script, as it doesn't publish any public functions. It is used
 *   internally to have a common place to handle general things, like internal
 *   command processing, and command-validation checks.
 * @api none
 */
class ScriptObject : public SimpleCountedObject {
friend class ScriptInstance;
friend class ScriptController;
friend class TestScriptController;
protected:
	/**
	 * A class that handles the current active instance. By instantiating it at
	 *  the beginning of a function with the current active instance, it remains
	 *  active till the scope of the variable closes. It then automatically
	 *  reverts to the active instance it was before instantiating.
	 */
	class ActiveInstance {
	friend class ScriptObject;
	public:
		ActiveInstance(ScriptInstance *instance);
		~ActiveInstance();
	private:
		ScriptInstance *last_active;    ///< The active instance before we go instantiated.
		ScriptAllocatorScope alc_scope; ///< Keep the correct allocator for the script instance activated

		static ScriptInstance *active;  ///< The global current active instance.
	};

public:
	/**
	 * Store the latest result of a DoCommand per company.
	 * @param res The result of the last command.
	 */
	static void SetLastCommandRes(bool res);

	/**
	 * Get the currently active instance.
	 * @return The instance.
	 */
	static class ScriptInstance *GetActiveInstance();

	/**
	 * Get a reference of the randomizer that brings this script random values.
	 * @param owner The owner/script to get the randomizer for. This defaults to ScriptObject::GetRootCompany()
	 */
	static Randomizer &GetRandomizer(Owner owner = ScriptObject::GetRootCompany());

	/**
	 * Initialize/reset the script random states. The state of the scripts are
	 * based on the current _random seed, but _random does not get changed.
	 */
	static void InitializeRandomizers();

private:
	static bool DoCommandImplementation(Commands cmd, TileIndex tile, CommandPayloadBase &&payload, Script_SuspendCallbackProc *callback, DoCommandIntlFlag intl_flags);

protected:
	template <Commands cmd>
	static bool DoCommand(TileIndex tile, typename CommandTraits<cmd>::PayloadType &&payload, Script_SuspendCallbackProc *callback = nullptr)
	{
		if constexpr (CommandTraits<cmd>::flags & CMD_CLIENT_ID) {
			SetCommandPayloadClientID(payload, (ClientID)UINT32_MAX);
		}
		return ScriptObject::DoCommandImplementation(cmd, tile, std::move(payload), callback, DCIF_TYPE_CHECKED);
	}

	template <Commands TCmd, typename T> struct ScriptDoCommandHelper;
	template <Commands TCmd, typename T> struct ScriptDoCommandHelperNoTile;

	template <Commands Tcmd, typename... Targs>
	struct ScriptDoCommandHelper<Tcmd, std::tuple<Targs...>> {
		using PayloadType = CmdPayload<Tcmd>;

		static bool Do(Script_SuspendCallbackProc *callback, TileIndex tile, Targs... args)
		{
			return ScriptObject::DoCommand<Tcmd>(tile, PayloadType::Make(std::forward<Targs>(args)...), callback);
		}

		static bool Do(TileIndex tile, Targs... args)
		{
			return ScriptObject::DoCommand<Tcmd>(tile, PayloadType::Make(std::forward<Targs>(args)...), nullptr);
		}
	};

	template <Commands Tcmd, typename... Targs>
	struct ScriptDoCommandHelperNoTile<Tcmd, std::tuple<Targs...>> {
		using PayloadType = CmdPayload<Tcmd>;

		static bool Do(Script_SuspendCallbackProc *callback, Targs... args)
		{
			return ScriptObject::DoCommand<Tcmd>(TileIndex{0}, PayloadType::Make(std::forward<Targs>(args)...), callback);
		}

		static bool Do(Targs... args)
		{
			return ScriptObject::DoCommand<Tcmd>(TileIndex{0}, PayloadType::Make(std::forward<Targs>(args)...), nullptr);
		}
	};

	/* Note that output_no_tile is used here instead of input_no_tile, because a tile index used only for error messages is not useful */
	template <Commands Tcmd>
	struct Command : public std::conditional_t<::CommandTraits<Tcmd>::output_no_tile,
			ScriptDoCommandHelperNoTile<Tcmd, typename ::CmdPayload<Tcmd>::Tuple>,
			ScriptDoCommandHelper<Tcmd, typename ::CmdPayload<Tcmd>::Tuple>> {};

	/**
	 * Store the latest command executed by the script.
	 */
	static void SetLastCommand(Commands cmd, TileIndex tile, CallbackParameter cb_param);

	/**
	 * Check if it's the latest command executed by the script.
	 */
	static bool CheckLastCommand(Commands cmd, TileIndex tile, CallbackParameter cb_param);

	/**
	 * Sets the DoCommand costs counter to a value.
	 */
	static void SetDoCommandCosts(Money value);

	/**
	 * Increase the current value of the DoCommand costs counter.
	 */
	static void IncreaseDoCommandCosts(Money value);

	/**
	 * Get the current DoCommand costs counter.
	 */
	static Money GetDoCommandCosts();

	/**
	 * Set the DoCommand last error.
	 */
	static void SetLastError(ScriptErrorType last_error);

	/**
	 * Get the DoCommand last error.
	 */
	static ScriptErrorType GetLastError();

	/**
	 * Set the road type.
	 */
	static void SetRoadType(RoadType road_type);

	/**
	 * Get the road type.
	 */
	static RoadType GetRoadType();

	/**
	 * Set the rail type.
	 */
	static void SetRailType(RailType rail_type);

	/**
	 * Get the rail type.
	 */
	static RailType GetRailType();

	/**
	 * Set the current mode of your script to this proc.
	 */
	static void SetDoCommandMode(ScriptModeProc *proc, ScriptObject *instance);

	/**
	 * Get the current mode your script is currently under.
	 */
	static ScriptModeProc *GetDoCommandMode();

	/**
	 * Get the instance of the current mode your script is currently under.
	 */
	static ScriptObject *GetDoCommandModeInstance();

	/**
	 * Set the current async mode of your script to this proc.
	 */
	static void SetDoCommandAsyncMode(ScriptAsyncModeProc *proc, ScriptObject *instance);

	/**
	 * Get the current async mode your script is currently under.
	 */
	static ScriptModeProc *GetDoCommandAsyncMode();

	/**
	 * Get the instance of the current async mode your script is currently under.
	 */
	static ScriptObject *GetDoCommandAsyncModeInstance();

	/**
	 * Set the delay of the DoCommand.
	 */
	static void SetDoCommandDelay(uint ticks);

	/**
	 * Get the delay of the DoCommand.
	 */
	static uint GetDoCommandDelay();

	/**
	 * Get the latest result of a DoCommand.
	 */
	static bool GetLastCommandRes();

	/**
	 * Store a allow_do_command per company.
	 * @param allow The new allow.
	 */
	static void SetAllowDoCommand(bool allow);

	/**
	 * Get the internal value of allow_do_command. This can differ
	 * from CanSuspend() if the reason we are not allowed
	 * to execute a DoCommand is in squirrel and not the API.
	 * In that case use this function to restore the previous value.
	 * @return True iff DoCommands are allowed in the current scope.
	 */
	static bool GetAllowDoCommand();

	/**
	 * Set the current company to execute commands for or request
	 *  information about.
	 * @param company The new company.
	 */
	static void SetCompany(::CompanyID company);

	/**
	 * Get the current company we are executing commands for or
	 *  requesting information about.
	 * @return The current company.
	 */
	static ::CompanyID GetCompany();

	/**
	 * Get the root company, the company that the script really
	 *  runs under / for.
	 * @return The root company.
	 */
	static ::CompanyID GetRootCompany();

	/**
	 * Set the cost of the last command.
	 */
	static void SetLastCost(Money last_cost);

	/**
	 * Get the cost of the last command.
	 */
	static Money GetLastCost();

	/**
	 * Set the result data of the last command.
	 */
	static void SetLastCommandResultData(uint32_t last_result);

	/**
	 * Clear the result data of the last command.
	 */
	static void ClearLastCommandResultData();

	/**
	 * Get the result data of the last command, or a default value if there wasn't any.
	 */
	template <typename T>
	static T GetLastCommandResultData(T default_value)
	{
		auto res = ScriptObject::GetLastCommandResultDataRaw();
		return res.second ? static_cast<T>(res.first) : default_value;
	}

	/**
	 * Set a variable that can be used by callback functions to pass information.
	 */
	static void SetCallbackVariable(int index, int value);

	/**
	 * Get the variable that is used by callback functions to pass information.
	 */
	static int GetCallbackVariable(int index);

	/**
	 * Can we suspend the script at this moment?
	 */
	static bool CanSuspend();

	/**
	 * Get the pointer to store event data in.
	 */
	static void *&GetEventPointer();

	/**
	 * Get the pointer to store log message in.
	 */
	static ScriptLogTypes::LogData &GetLogData();

	/**
	 * Get an allocated string with all control codes stripped off.
	 */
	static std::string GetString(StringID string);

	static bool IsNewUniqueLogMessage(const std::string &msg);

	static void RegisterUniqueLogMessage(std::string &&msg);

private:
	static std::pair<uint32_t, bool> GetLastCommandResultDataRaw();

	static Randomizer random_states[OWNER_END]; ///< Random states for each of the scripts (game script uses OWNER_DEITY)
};

/**
 * Internally used class to automate the ScriptObject reference counting.
 * @api -all
 */
template <typename T>
class ScriptObjectRef {
private:
	T *data; ///< The reference counted object.
public:
	/**
	 * Create the reference counter for the given ScriptObject instance.
	 * @param data The underlying object.
	 */
	ScriptObjectRef(T *data) : data(data)
	{
		if (this->data != nullptr) this->data->AddRef();
	}

	/* No copy constructor. */
	ScriptObjectRef(const ScriptObjectRef<T> &ref) = delete;

	/* Move constructor. */
	ScriptObjectRef(ScriptObjectRef<T> &&ref) noexcept : data(std::exchange(ref.data, nullptr))
	{
	}

	/* No copy assignment. */
	ScriptObjectRef& operator=(const ScriptObjectRef<T> &other) = delete;

	/* Move assignment. */
	ScriptObjectRef& operator=(ScriptObjectRef<T> &&other) noexcept
	{
		std::swap(this->data, other.data);
		return *this;
	}

	/**
	 * Release the reference counted object.
	 */
	~ScriptObjectRef()
	{
		if (this->data != nullptr) this->data->Release();
	}

	/**
	 * Dereferencing this reference returns a reference to the reference
	 * counted object
	 * @return Reference to the underlying object.
	 */
	T &operator*()
	{
		return *this->data;
	}

	/**
	 * The arrow operator on this reference returns the reference counted object.
	 * @return Pointer to the underlying object.
	 */
	T *operator->()
	{
		return this->data;
	}

	/**
	 * The arrow operator on this reference returns the reference counted object.
	 * @return Pointer to the underlying object.
	 */
	const T *operator->() const
	{
		return this->data;
	}
};

#endif /* SCRIPT_OBJECT_HPP */
