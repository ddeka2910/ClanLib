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
**    Magnus Norddahl
*/

#include "UI/precomp.h"
#include "API/UI/Controller/window_manager.h"
#include "API/UI/UIThread/ui_thread.h"
#include "API/Display/Window/display_window_description.h"
#include "API/Display/Window/display_window.h"
#include "API/Display/2D/canvas.h"
#include "API/Display/2D/image.h"
#include "API/Display/System/run_loop.h"
#include "API/Display/Image/pixel_buffer.h"
#include "API/Core/IOData/path_help.h"
#include <map>

namespace clan
{
	class WindowManagerImpl
	{
	public:
		bool exit_on_last_close = true;
		std::map<WindowController *, std::shared_ptr<WindowController>> windows;
	};

	/////////////////////////////////////////////////////////////////////////

	class WindowControllerImpl
	{
	public:
		std::string title;
		Sizef initial_size;
		bool frame_geometry = true;
		bool resizable = true;
		std::vector<std::string> icon_images;
		std::shared_ptr<View> root_view = std::make_shared<View>();

		WindowManager *manager = nullptr;
		std::shared_ptr<TopLevelWindow> window;
		std::weak_ptr<View> modal_owner;
	};

	/////////////////////////////////////////////////////////////////////////

	WindowManager::WindowManager() : impl(new WindowManagerImpl)
	{
	}

	WindowManager::~WindowManager()
	{
	}
	void WindowManager::set_exit_on_last_close(bool enable)
	{
		impl->exit_on_last_close = enable;
	}

	void WindowManager::present_main(const std::shared_ptr<WindowController> &controller, DisplayWindowDescription* desc, WindowShowType show_type)
	{
		auto& controller_impl = controller->impl;
		if (controller_impl->manager)
			return;

		DisplayWindowDescription simple_desc;
		if (!desc) {
			simple_desc.set_main_window();
			simple_desc.set_visible(false);
			simple_desc.set_title(controller->title());
			simple_desc.set_allow_resize(controller_impl->resizable);

			if (controller_impl->initial_size != Sizef())
				simple_desc.set_size(controller_impl->initial_size, !controller_impl->frame_geometry);

			desc = &simple_desc;
		}

		controller_impl->manager = this;
		controller_impl->window = std::make_shared<TopLevelWindow>(*desc);
		auto& root_view = controller->root_view();
		controller_impl->window->set_root_view(root_view);

		DisplayWindow display_window = controller_impl->window->display_window();
		controller->slots.connect(display_window.sig_window_close(), bind_member(controller.get(), &WindowController::dismiss));

		impl->windows[controller.get()] = controller;

		if (controller_impl->initial_size == Sizef())
		{
			Canvas canvas = root_view->canvas();
			float width = root_view->preferred_width(canvas);
			float height = root_view->preferred_height(canvas, width);
			Rectf content_box(0.0f, 0.0f, width, height);
			Rectf margin_box = ViewGeometry::from_content_box(root_view->style_cascade(), content_box).margin_box();
			display_window.set_size(margin_box.get_width(), margin_box.get_height(), true);
		}

		auto icon_images = controller_impl->icon_images;
		if (!icon_images.empty())
		{
			display_window.set_large_icon(PixelBuffer(icon_images.front()));
			display_window.set_large_icon(PixelBuffer(icon_images.back()));
		}
	
		controller_impl->window->show(show_type);
	}

	void WindowManager::present_modal(View *owner, const std::shared_ptr<WindowController> &controller, DisplayWindowDescription* desc)
	{
		auto& controller_impl = controller->impl;
		if (controller_impl->manager)
			return;

		Pointf screen_pos = owner->to_screen_pos(owner->geometry().content_box().get_center());

		DisplayWindowDescription simple_desc;
		DisplayWindow owner_display_window = owner->view_tree()->display_window();
		if (!desc) {
			simple_desc.set_dialog_window();
			simple_desc.set_visible(false);
			simple_desc.set_title(controller->title());
			simple_desc.show_minimize_button(false);
			simple_desc.show_maximize_button(false);
			simple_desc.set_allow_resize(controller_impl->resizable);

			if (owner_display_window)
				simple_desc.set_owner_window(owner_display_window);

			if (controller_impl->initial_size != Sizef())
				simple_desc.set_size(controller_impl->initial_size, !controller_impl->frame_geometry);

			desc = &simple_desc;
		}

		controller_impl->modal_owner = owner->shared_from_this();
		controller_impl->manager = this;
		controller_impl->window = std::make_shared<TopLevelWindow>(*desc);
		auto& root_view = controller->root_view();
		controller_impl->window->set_root_view(root_view);

		DisplayWindow controller_display_window = controller_impl->window->display_window();
		controller->slots.connect(controller_display_window.sig_window_close(), bind_member(controller.get(), &WindowController::dismiss));

		impl->windows[controller.get()] = controller;

		if (controller_impl->initial_size == Sizef())
		{
			Canvas canvas = root_view->canvas();
			float width = root_view->preferred_width(canvas);
			float height = root_view->preferred_height(canvas, width);
			Rectf content_box(screen_pos.x - width * 0.5f, screen_pos.y - height * 0.5f, screen_pos.x + width * 0.5f, screen_pos.y + height * 0.5f);
			Rectf margin_box = ViewGeometry::from_content_box(root_view->style_cascade(), content_box).margin_box();
			controller_display_window.set_position(margin_box, true);
		}

		auto icon_images = controller_impl->icon_images;
		if (!icon_images.empty())
		{
			controller_display_window.set_large_icon(PixelBuffer(icon_images.front()));
			controller_display_window.set_large_icon(PixelBuffer(icon_images.back()));
		}

		controller_impl->window->show(WindowShowType::show);
		if (owner_display_window)
			owner_display_window.set_enabled(false);
	}

	void WindowManager::present_popup(View *owner, const Pointf &pos, const std::shared_ptr<WindowController> &controller, DisplayWindowDescription* desc)
	{
		auto& controller_impl = controller->impl;
		if (controller_impl->manager)
			return;

		Pointf screen_pos = owner->to_screen_pos(pos);

		DisplayWindowDescription simple_desc;
		DisplayWindow owner_display_window = owner->view_tree()->display_window();
		if (!desc) {
			simple_desc.set_popup_window();
			simple_desc.set_visible(false);
			simple_desc.set_topmost(true);
			simple_desc.set_no_activate(true);
			simple_desc.show_caption(false);
			simple_desc.show_sysmenu(false);
			simple_desc.show_minimize_button(false);
			simple_desc.show_maximize_button(false);

			if (owner_display_window)
				simple_desc.set_owner_window(owner_display_window);

			if (controller_impl->initial_size != Sizef())
				simple_desc.set_size(controller_impl->initial_size, !controller_impl->frame_geometry);

			desc = &simple_desc;
		}

		controller_impl->manager = this;
		controller_impl->window = std::make_shared<TopLevelWindow>(*desc);
		auto& root_view = controller->root_view();
		controller_impl->window->set_root_view(root_view);

		if (owner_display_window)
			controller->slots.connect(owner_display_window.sig_lost_focus(), bind_member(controller.get(), &WindowController::dismiss));

		impl->windows[controller.get()] = controller;

		if (controller_impl->initial_size == Sizef())
		{
			Canvas canvas = root_view->canvas();
			float width = root_view->preferred_width(canvas);
			float height = root_view->preferred_height(canvas, width);
			Rectf content_box(screen_pos.x, screen_pos.y, screen_pos.x + width, screen_pos.y + height);
			Rectf margin_box = ViewGeometry::from_content_box(root_view->style_cascade(), content_box).margin_box();

			DisplayWindow controller_display_window = controller_impl->window->display_window();
			controller_display_window.set_position(margin_box, false);
		}

		controller_impl->window->show(WindowShowType::show_no_activate);
	}

	/// Translates a call to all top-level windows.
	void WindowManager::flip(int interval) {
		for (auto& map_item : impl->windows) {
			auto& top_level_window = map_item.first->impl->window;
			top_level_window->display_window().flip(interval);
		}
	}


	/////////////////////////////////////////////////////////////////////////

	WindowController::WindowController() : impl(new WindowControllerImpl)
	{
	}

	WindowController::~WindowController()
	{
	}

	const std::shared_ptr<View> &WindowController::root_view() const
	{
		return impl->root_view;
	}

	void WindowController::set_root_view(std::shared_ptr<View> root_view)
	{
		impl->root_view = root_view;
	}

	const std::string &WindowController::title() const
	{
		return impl->title;
	}

	void WindowController::set_title(const std::string &title)
	{
		impl->title = title;
		if (impl->window)
		{
			DisplayWindow display_window = impl->window->display_window();
			if (display_window)
				display_window.set_title(title);
		}
	}

	void WindowController::set_frame_size(const Sizef &size, bool resizable)
	{
		impl->initial_size = size;
		impl->frame_geometry = true;
		impl->resizable = resizable;
		if (impl->window)
		{
			DisplayWindow display_window = impl->window->display_window();
			if (display_window)
				display_window.set_size(size.width, size.height, false);
		}
	}

	void WindowController::set_content_size(const Sizef &size, bool resizable)
	{
		impl->initial_size = size;
		impl->frame_geometry = false;
		impl->resizable = resizable;
		if (impl->window)
		{
			DisplayWindow display_window = impl->window->display_window();
			if (display_window)
				display_window.set_size(size.width, size.height, true);
		}
	}

	void WindowController::set_resizable(bool resizable)
	{
		impl->resizable = resizable;
	}

	bool WindowController::resizable()
	{
		return impl->resizable;
	}


	void WindowController::set_icon(const std::vector<std::string> &icon_images)
	{
		impl->icon_images = icon_images;
		if (impl->window)
		{
			DisplayWindow display_window = impl->window->display_window();

			if (display_window && !icon_images.empty())
			{
				display_window.set_large_icon(PixelBuffer(icon_images.front()));
				display_window.set_large_icon(PixelBuffer(icon_images.back()));
			}
			
		}
	}

	void WindowController::dismiss()
	{
		if (impl->manager)
		{
			auto modal_owner = impl->modal_owner.lock();
			if (modal_owner && modal_owner->view_tree())
			{
				DisplayWindow display_window = modal_owner->view_tree()->display_window();
				if (display_window)
				{
					display_window.set_enabled(true);
					if (impl->window->display_window().has_focus())
						display_window.show(true); // activate parent to workaround bug in Windows in some situations
				}
			}

			auto manager = impl->manager;

			// Reset fields before erase because 'this' might be destroyed if 'windows' had the last reference
			impl->window.reset();
			impl->modal_owner.reset();

			auto &windows = manager->impl->windows;
			auto it = windows.find(this);
			if (it != windows.end())
				windows.erase(it);

			if (manager->impl->exit_on_last_close && windows.empty())
				RunLoop::exit();
		}
	}

	void WindowController::immediate_update()
	{
		impl->window->immediate_update();
	}
}
