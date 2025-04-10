/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_survey.h Part of the network protocol handling opt-in survey. */

#ifndef NETWORK_SURVEY_H
#define NETWORK_SURVEY_H

#include <condition_variable>
#include <mutex>
#include "core/http.h"

/**
 * Socket handler for the survey connection
 */
class NetworkSurveyHandler : public HTTPCallback {
protected:
	void OnFailure() override;
	void OnReceiveData(UniqueBuffer<char> data) override;
	bool IsCancelled() const override { return false; }

public:
	enum class Reason : uint8_t {
		PREVIEW, ///< User is previewing the survey result.
		LEAVE, ///< User is leaving the game (but not exiting the application).
		EXIT, ///< User is exiting the application.
		CRASH, ///< Game crashed.
	};

	void Transmit(Reason reason, bool blocking = false);
	std::string CreatePayload(Reason reason, bool for_preview = false);

	constexpr static bool IsSurveyPossible()
	{
#if !defined(SURVEY_KEY)
		/* Without a survey key, we cannot send a payload; so we disable the survey. */
		return false;
#else
		return true;
#endif /* SURVEY_KEY */
	}

private:
	std::mutex mutex; ///< Mutex for the condition variable.
	std::atomic<bool> transmitted; ///< Whether the survey has been transmitted.
	std::condition_variable transmitted_cv; ///< Condition variable to inform changes to transmitted.
};

extern NetworkSurveyHandler _survey;

#endif /* NETWORK_SURVEY_H */
