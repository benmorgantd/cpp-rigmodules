#include "AssetId.h"

namespace AssetId
{
	bool isValidName(const std::string& name)
	{
		if (name.empty())
		{
			return false;
		}
		for (char c : name)
		{
			const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			if (!valid)
			{
				return false;
			}
		}
		return true;
	}

	bool isValid(const std::string& assetId)
	{
		std::size_t tokenStart = 0;
		while (true)
		{
			const std::size_t separator = assetId.find('\\', tokenStart);
			const std::size_t tokenEnd = separator == std::string::npos ? assetId.size() : separator;
			if (!isValidName(assetId.substr(tokenStart, tokenEnd - tokenStart)))
			{
				return false;
			}
			if (separator == std::string::npos)
			{
				return true;
			}
			tokenStart = separator + 1;
		}
	}

	std::string name(const std::string& assetId)
	{
		const std::size_t lastSeparator = assetId.rfind('\\');
		return lastSeparator == std::string::npos ? assetId : assetId.substr(lastSeparator + 1);
	}
}
