/*
**  ClanLib SDK
**  Copyright (c) 1997-2020 The ClanLib Team
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
**  Note: Some of the libraries ClanLib may link to may have additional
**  requirements or restrictions.
**
**  File Author(s):
**
**    Harry Storbacka
*/

#include "Display/precomp.h"
#include "API/Display/screen_info.h"
#include "screen_info_provider.h"
#include "API/Core/Math/rect.h"

#ifdef WIN32
 #include "Platform/Win32/screen_info_provider_win32.h"
#endif

namespace clan
{
	class ScreenInfo_Impl
	{
	public:
		ScreenInfo_Impl()
		{
#ifdef WIN32
			provider = new ScreenInfoProvider_Win32;
#else
			provider = nullptr;
#endif
		}

		~ScreenInfo_Impl()
		{
			delete provider;
		}

		ScreenInfoProvider *provider;
	};

	ScreenInfo::ScreenInfo()
		: impl(std::make_shared<ScreenInfo_Impl>())
	{
	}

	std::vector<Rectf> ScreenInfo::get_screen_geometries(int &primary_screen_index) const
	{
		return impl->provider->get_screen_geometries(primary_screen_index);
	}
}
