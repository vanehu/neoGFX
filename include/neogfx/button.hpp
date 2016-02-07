// button.hpp
/*
  neogfx C++ GUI Library
  Copyright(C) 2016 Leigh Johnston
  
  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "neogfx.hpp"
#include "widget.hpp"
#include "label.hpp"
#include "horizontal_layout.hpp"
#include "event.hpp"

namespace neogfx
{
	class button : public widget
	{
	public:
		event<> pressed;
	private:
		class our_layout : public horizontal_layout
		{
		public:
			our_layout(button& aParent);
		protected:
			virtual neogfx::size_policy size_policy() const;
		protected:
			virtual neogfx::margins margins() const;
		};
	public:
		button(const std::string& aText = std::string(), alignment aAlignment = alignment::Left | alignment::VCentre);
		button(i_widget& aParent, const std::string& aText = std::string(), alignment aAlignment = alignment::Left | alignment::VCentre);
		button(i_layout& aLayout, const std::string& aText = std::string(), alignment aAlignment = alignment::Left | alignment::VCentre);
		~button();
	public:
		const neogfx::label& label() const;
		neogfx::label& label();
		const text_widget& text() const;
		text_widget& text();
	protected:
		virtual void mouse_button_released(mouse_button aButton, const point& aPosition);
	protected:
		virtual void key_pressed(scan_code_e aScanCode, key_code_e aKeyCode, key_modifiers_e aKeyModifiers);
	protected:
		virtual void handle_pressed();
	private:
		our_layout iLayout;
		neogfx::label iLabel;
	};
}