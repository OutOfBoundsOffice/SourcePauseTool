#pragma once
#include <iomanip>
#include <random>
#include <sstream>
#include "math.hpp"
#include "string_utils.hpp"

namespace scripts
{
	class SearchDoneException
	{
	};

	enum class SearchResult
	{
		NoSearch,
		Success,
		Fail
	};
	enum class SearchType
	{
		None,
		Lowest,
		Range,
		Highest,
		Random,
		RandomLowest,
		RandomHighest
	};

	template<typename T>
	class RangeVariable
	{
	public:
		RangeVariable();
		void Select(SearchResult lastResult, SearchType type);
		std::string GetValue();
		std::string GetRangeString();
		void ParseInput(const std::string& value, bool angle);
		void ParseValues(const std::string& value);

	private:
		void SelectLow(SearchResult lastResult);
		void SelectHigh(SearchResult lastResult);
		void SelectRandom();
		void SelectMiddle();
		T Normalize(T value);
		T GetValueForIndex(int index);

		bool isAngle;
		T initialLow;
		T initialHigh;
		int lowIndex;
		int valueIndex;
		int highIndex;
		T increment;
		int lastIndex;

		std::mt19937 rng;
		std::uniform_int_distribution<int> uniformRandom;
	};

	template<typename T>
	inline T RangeVariable<T>::GetValueForIndex(int index)
	{
		auto value = initialLow + index * increment;
		return Normalize(value);
	}

	template<typename T>
	inline std::string RangeVariable<T>::GetValue()
	{
		return std::to_string(GetValueForIndex(valueIndex));
	}

	template<typename T>
	inline std::string RangeVariable<T>::GetRangeString()
	{
		std::ostringstream os;

		os << std::setprecision(9) << "[low: " << GetValueForIndex(lowIndex) << ", value: " << GetValueForIndex(valueIndex)
		   << ", high: " << GetValueForIndex(highIndex) << "]";

		return os.str();
	}

	template<typename T>
	inline void RangeVariable<T>::ParseInput(const std::string& value, bool angle)
	{
		isAngle = angle;
		ParseValues(value);
		SelectMiddle();
		uniformRandom = std::uniform_int_distribution<int>(lowIndex, highIndex);
	}

	template<typename T>
	inline void RangeVariable<T>::ParseValues(const std::string& value)
	{
		GetTriplet(value, initialLow, initialHigh, increment, '|');

		if (increment <= 0)
			throw std::exception("increment cannot be <= 0");
		else if (initialLow >= initialHigh)
			throw std::exception("Low was higher than high");

		lowIndex = 0;
		highIndex = static_cast<int>((initialHigh - initialLow) / increment);
	}

	template<typename T>
	inline RangeVariable<T>::RangeVariable()
	{
		std::random_device rd;
		rng = std::mt19937(rd());
	}

	template<typename T>
	inline void RangeVariable<T>::Select(SearchResult lastResult, SearchType type)
	{
		if (lastResult == SearchResult::NoSearch)
			return;

		switch (type)
		{
		case SearchType::Lowest:
			SelectLow(lastResult);
			break;
		case SearchType::Highest:
			SelectHigh(lastResult);
			break;
		case SearchType::Random:
		case SearchType::RandomHighest:
		case SearchType::RandomLowest:
			SelectRandom();
			break;
		default:
			throw std::exception("Search type is not implemented or not in search mode");
			break;
		}

		// Search stuck in place, stop searching
		if (type != SearchType::Random && lastIndex == valueIndex)
		{
			throw SearchDoneException();
		}

		lastIndex = valueIndex;
	}

	template<typename T>
	inline void RangeVariable<T>::SelectLow(SearchResult lastResult)
	{
		switch (lastResult)
		{
		case SearchResult::Fail:
			lowIndex = valueIndex;
			break;
		case SearchResult::Success:
			highIndex = valueIndex;
			break;
		default:
			throw std::exception("Unexpected search result received");
		}

		SelectMiddle();
	}

	template<typename T>
	inline void RangeVariable<T>::SelectHigh(SearchResult lastResult)
	{
		switch (lastResult)
		{
		case SearchResult::Success:
			lowIndex = valueIndex;
			break;
		case SearchResult::Fail:
			highIndex = valueIndex;
			break;
		default:
			throw std::exception("Unexpected search result received");
		}

		SelectMiddle();
	}

	template<typename T>
	inline void RangeVariable<T>::SelectRandom()
	{
		valueIndex = uniformRandom(rng);
	}

	template<typename T>
	inline void RangeVariable<T>::SelectMiddle()
	{
		valueIndex = (lowIndex + highIndex) / 2;
	}

	template<typename T>
	inline T RangeVariable<T>::Normalize(T value)
	{
		return value;
	}

	template<> 
	inline float RangeVariable<float>::Normalize(float value)
	{
		if (isAngle)
			value = static_cast<float>(utils::NormalizeDeg(static_cast<double>(value)));

		return value;
	}
} // namespace scripts
