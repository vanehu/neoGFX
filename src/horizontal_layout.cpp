// horizontal_layout.cpp
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

#include "neogfx.hpp"
#include <unordered_map>
#include <unordered_set>
#include <boost/pool/pool_alloc.hpp>
#include <neolib/bresenham_counter.hpp>
#include "horizontal_layout.hpp"
#include "i_widget.hpp"
#include "spacer.hpp"

namespace neogfx
{
	horizontal_layout::horizontal_layout(i_widget& aParent, alignment aVerticalAlignment) :
		layout(aParent), iVerticalAlignment(aVerticalAlignment)
	{
	}

	horizontal_layout::horizontal_layout(i_layout& aParent, alignment aVerticalAlignment) :
		layout(aParent), iVerticalAlignment(aVerticalAlignment)
	{
	}

	i_spacer& horizontal_layout::add_spacer()
	{
		auto s = std::make_shared<horizontal_spacer>();
		add_spacer(s);
		return *s;
	}

	i_spacer& horizontal_layout::add_spacer(uint32_t aPosition)
	{
		auto s = std::make_shared<horizontal_spacer>();
		add_spacer(aPosition, s);
		return *s;
	}

	size horizontal_layout::minimum_size() const
	{
		if (items_visible() == 0)
			return size{};
		size result;
		uint32_t itemsZeroSized = 0;
		for (const auto& item : items())
		{
			if (!item.visible())
				continue;
			if (item.get().is<item::widget_pointer>() && (item.minimum_size().cx == 0.0 || item.minimum_size().cy == 0.0))
			{
				++itemsZeroSized;
				continue;
			}
			result.cy = std::max(result.cy, item.minimum_size().cy);
			result.cx += item.minimum_size().cx;
		}
		result.cx += (margins().left + margins().right);
		result.cy += (margins().top + margins().bottom);
		if (result.cx != std::numeric_limits<size::dimension_type>::max() && (items_visible() - itemsZeroSized) > 0)
			result.cx += (spacing().cx * (items_visible() - itemsZeroSized - 1));
		result.cx = std::max(result.cx, layout::minimum_size().cx);
		result.cy = std::max(result.cy, layout::minimum_size().cy);
		return result;
	}

	size horizontal_layout::maximum_size() const
	{
		if (items_visible(static_cast<item_type_e>(ItemTypeWidget | ItemTypeLayout | ItemTypeSpacer)) == 0)
			return size{};
		size result{ std::numeric_limits<size::dimension_type>::max(), 0.0 };
		for (const auto& item : items())
		{
			if (!item.visible())
				continue;
			result.cy = std::max(result.cy, item.maximum_size().cy);
			auto cx = std::min(result.cx, item.maximum_size().cx);
			if (cx != std::numeric_limits<size::dimension_type>::max())
				result.cx += cx;
			else
				result.cx = std::numeric_limits<size::dimension_type>::max();
		}
		if (result.cx != std::numeric_limits<size::dimension_type>::max())
			result.cx += (margins().left + margins().right);
		if (result.cy != std::numeric_limits<size::dimension_type>::max())
			result.cy += (margins().top + margins().bottom);
		if (result.cx != std::numeric_limits<size::dimension_type>::max() && items_visible() > 0)
			result.cx += (spacing().cx * (items_visible() - 1 - spacer_count()));
		if (result.cx != std::numeric_limits<size::dimension_type>::max())
			result.cx = std::min(result.cx, layout::maximum_size().cx);
		if (result.cy != std::numeric_limits<size::dimension_type>::max())
			result.cy = std::min(result.cy, layout::maximum_size().cy);
		return result;
	}

	void horizontal_layout::layout_items(const point& aPosition, const size& aSize)
	{
		if (!enabled())
			return;
		if (items_visible(static_cast<item_type_e>(ItemTypeWidget | ItemTypeLayout | ItemTypeSpacer)) == 0)
			return;
		size availableSize = aSize;
		availableSize.cx -= (margins().left + margins().right);
		availableSize.cy -= (margins().top + margins().bottom);
		uint32_t itemsZeroSized = 0;
		if (aSize.cx <= minimum_size().cx)
		{
			for (const auto& item : items())
			{
				if (!item.visible())
					continue;
				if (item.get().is<item::widget_pointer>() && (item.minimum_size().cx == 0.0 || item.minimum_size().cy == 0.0))
					++itemsZeroSized;
			}
		}
		if (items_visible() - itemsZeroSized > 0)
			availableSize.cx -= (spacing().cx * (items_visible() - itemsZeroSized - 1));
		size::dimension_type leftover = availableSize.cx;
		size::dimension_type eachLeftover = std::floor(leftover / items_visible());
		size totalSpacerWeight;
		enum disposition_e { Unknown, Normal, TooSmall, TooBig };
		std::unordered_map<const item*, disposition_e, std::hash<const item*>, std::equal_to<const item*>, boost::pool_allocator<std::pair<const item*, disposition_e>>> itemDispositions;
		std::unordered_set<const item*, std::hash<const item*>, std::equal_to<const item*>, boost::pool_allocator<const item*>> spacersUsingLeftover;
		auto items_not_using_leftover = [&itemDispositions]() -> std::size_t
		{
			std::size_t result = 0;
			for (auto& i : itemDispositions)
				if (i.second == TooSmall || i.second == TooBig)
					++result;
			return result;
		};
		bool done = false;
		while (!done)
		{
			done = true;
			for (const auto& item : items())
			{
				if (!item.visible())
					continue;
				if (spacersUsingLeftover.find(&item) != spacersUsingLeftover.end())
					continue;
				if (item.get().is<item::spacer_pointer>() && item.maximum_size().cx >= leftover)
				{
					if (spacersUsingLeftover.empty())
					{
						itemDispositions.clear();
						leftover = availableSize.cx;
						totalSpacerWeight = size{};
						eachLeftover = 0.0;
					}
					spacersUsingLeftover.insert(&item);
					totalSpacerWeight += static_variant_cast<item::spacer_pointer>(item.get())->weight();
					done = false;
					break;
				}
				else if (!item.get().is<item::spacer_pointer>() && !spacersUsingLeftover.empty())
				{
					if (itemDispositions[&item] != TooBig)
					{
						if (itemDispositions[&item] == TooSmall)
							leftover += item.maximum_size().cx;
						itemDispositions[&item] = TooBig;
						leftover -= item.minimum_size().cx;
						done = false;
					}
				}
				else if (item.maximum_size().cx < eachLeftover)
				{
					if (itemDispositions[&item] != TooSmall && itemDispositions[&item] != Normal)
					{
						if (itemDispositions[&item] == TooBig)
							leftover += item.minimum_size().cx;
						itemDispositions[&item] = TooSmall;
						leftover -= item.maximum_size().cx;
						if (spacersUsingLeftover.empty())
							eachLeftover = std::floor(leftover / (items_visible() - items_not_using_leftover()));
						done = false;
					}
				}
				else if (item.minimum_size().cx > eachLeftover)
				{
					if (itemDispositions[&item] != TooBig)
					{
						if (itemDispositions[&item] == TooSmall)
							leftover += item.maximum_size().cx;
						itemDispositions[&item] = TooBig;
						leftover -= item.minimum_size().cx;
						if (spacersUsingLeftover.empty())
							eachLeftover = std::floor(leftover / (items_visible() - items_not_using_leftover()));
						done = false;
					}
				}
				else if (itemDispositions[&item] != Normal)
				{
					if (itemDispositions[&item] == TooSmall)
						leftover += item.maximum_size().cx;
					else if (itemDispositions[&item] == TooBig)
						leftover += item.minimum_size().cx;
					itemDispositions[&item] = Normal;
					if (spacersUsingLeftover.empty())
						eachLeftover = std::floor(leftover / (items_visible() - items_not_using_leftover()));
					done = false;
				}
			}
		}
		if (leftover < 0.0)
		{
			leftover = 0.0;
			eachLeftover = 0.0;
		}
		uint32_t numberUsingLeftover = items_visible(static_cast<item_type_e>(ItemTypeWidget | ItemTypeLayout | ItemTypeSpacer)) - items_not_using_leftover();
		uint32_t bitsLeft = static_cast<int32_t>(leftover - (eachLeftover * numberUsingLeftover));
		if (!spacersUsingLeftover.empty())
		{
			size::dimension_type totalIntegralAmount = 0.0;
			for (const auto& s : spacersUsingLeftover)
				totalIntegralAmount += std::floor(static_variant_cast<const item::spacer_pointer&>(s->get())->weight().cx / totalSpacerWeight.cx * leftover);
			bitsLeft = static_cast<int32_t>(leftover - totalIntegralAmount);
		}
		neolib::bresenham_counter<int32_t> bits(bitsLeft, numberUsingLeftover);
		uint32_t previousBit = 0;
		point nextPos = aPosition;
		nextPos.x += margins().left;
		nextPos.y += margins().top;
		for (auto& item : items())
		{
			if (!item.visible())
				continue;
			size s{ 0, std::min(std::max(item.minimum_size().cy, availableSize.cy), item.maximum_size().cy) };
			point alignmentAdjust;
			switch (iVerticalAlignment)
			{
			case alignment::Top:
				alignmentAdjust.y = 0;
				break;
			case alignment::Bottom:
				alignmentAdjust.y = availableSize.cy - s.cy;
				break;
			case alignment::VCentre:
			default:
				alignmentAdjust.y = std::ceil((availableSize.cy - s.cy) / 2.0);
				break;
			}
			if (itemDispositions[&item] == TooBig)
				s.cx = item.minimum_size().cx;
			else if (itemDispositions[&item] == TooSmall)
				s.cx = item.maximum_size().cx;
			else if (spacersUsingLeftover.find(&item) != spacersUsingLeftover.end())
			{
				uint32_t bit = bitsLeft != 0 ? bits() : 0;
				s.cx = std::floor(static_variant_cast<const item::spacer_pointer&>(item.get())->weight().cx / totalSpacerWeight.cx * leftover) + static_cast<size::dimension_type>(bit - previousBit);
				previousBit = bit;
			}
			else
			{
				uint32_t bit = bitsLeft != 0 ? bits() : 0;
				s.cx = eachLeftover + static_cast<size::dimension_type>(bit - previousBit);
				previousBit = bit;
			}
			item.layout(nextPos + alignmentAdjust, s);
			if (!item.get().is<item::spacer_pointer>() && (s.cx == 0.0 || s.cy == 0.0))
				continue;
			nextPos.x += s.cx;
			if (!item.get().is<item::spacer_pointer>())
				nextPos.x += spacing().cx;
		}
		owner()->layout_items_completed();
	}
}