#include "AssetId.h"

#include <cctype>

namespace AssetId
{
	bool isValid(const std::string& assetId)
	{
		bool tokenEmpty = true;
		for (char c : assetId)
		{
			if (c == '|')
			{
				if (tokenEmpty)
				{
					return false;
				}
				tokenEmpty = true;
			}
			else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
			{
				tokenEmpty = false;
			}
			else
			{
				return false;
			}
		}
		return !tokenEmpty;
	}

	std::string name(const std::string& assetId)
	{
		const std::size_t lastPipe = assetId.rfind('|');
		return lastPipe == std::string::npos ? assetId : assetId.substr(lastPipe + 1);
	}
}
