if ("GetTimeBetweenDates" in CompanyGoal) {
	CompanyGoal._GetTimeBetweenDates <- CompanyGoal.GetTimeBetweenDates;
	CompanyGoal.GetTimeBetweenDates <- function(start, end)
	{
		return CompanyGoal._GetTimeBetweenDates(start, start + ((end - start) / GSDate.GetDayLengthFactor()));
	}

	GSLog.Info("Day length compatibility in effect.");
}
