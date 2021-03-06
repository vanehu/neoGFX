// item_presentation_model.hpp
/*
  neogfx C++ GUI Library
  Copyright (c) 2015 Leigh Johnston.  All Rights Reserved.
  
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

#include <neogfx/neogfx.hpp>
#include <vector>
#include <deque>
#include <boost/algorithm/string.hpp>
#include <neolib/vecarray.hpp>
#include <neolib/segmented_array.hpp>
#include <neolib/observable.hpp>
#include <neolib/raii.hpp>
#include <neogfx/gfx/graphics_context.hpp>
#include <neogfx/app/app.hpp>
#include "item_model.hpp"
#include "i_item_presentation_model.hpp"

namespace neogfx
{
	template <typename ItemModel>
	class basic_item_presentation_model : public i_item_presentation_model, private i_item_model_subscriber, private neolib::observable<i_item_presentation_model_subscriber>
	{
	private:
		typedef ItemModel item_model_type;
		typedef typename item_model_type::container_traits::template rebind<item_presentation_model_index::row_type, cell_meta_type>::other container_traits;
		typedef typename container_traits::row_container_type row_container_type;
		typedef typename container_traits::container_type container_type;
		typedef boost::optional<i_scrollbar::value_type> optional_position;
	private:
		typedef std::unordered_map<item_model_index::row_type, item_presentation_model_index::row_type, std::hash<item_model_index::row_type>, std::equal_to<item_model_index::row_type>,
			typename container_traits::allocator_type::template rebind<std::pair<const item_model_index::row_type, item_presentation_model_index::row_type>>::other> row_map_type;
		typedef std::unordered_map<item_model_index::column_type, item_presentation_model_index::column_type, std::hash<item_model_index::column_type>, std::equal_to<item_model_index::column_type>,
			typename container_traits::allocator_type::template rebind<std::pair<const item_model_index::column_type, item_presentation_model_index::column_type>>::other> column_map_type;
		struct column_info
		{
			column_info(item_model_index::column_type aModelColumn) : modelColumn{ aModelColumn }, editable{ item_cell_editable::OnInputEvent } {}
			item_model_index::column_type modelColumn;
			item_cell_editable editable;
			mutable optional_dimension width;
			mutable boost::optional<std::string> headingText;
			mutable font headingFont;
			mutable optional_size headingExtents;
		};
		typedef typename container_traits::template rebind<item_presentation_model_index::row_type, column_info>::other::row_container_type column_info_container_type;
	public:
		basic_item_presentation_model() : iItemModel{ nullptr }, iInitializing{ false }, iFiltering{ false }
		{
			init();
		}
		basic_item_presentation_model(i_item_model& aItemModel) : iItemModel{ nullptr }, iInitializing{ false }, iFiltering{ false }
		{
			init();
			set_item_model(aItemModel);
		}
		~basic_item_presentation_model()
		{
			if (has_item_model())
				item_model().unsubscribe(*this);
			notify_observers(i_item_presentation_model_subscriber::NotifyModelDestroyed);
		}
	public:
		bool initializing() const override
		{
			return iInitializing;
		}
		bool has_item_model() const override
		{
			return iItemModel != nullptr;
		}
		i_item_model& item_model() const override
		{
			if (iItemModel == nullptr)
				throw no_item_model();
			return *iItemModel;
		}
		void set_item_model(i_item_model& aItemModel) override
		{
			if (iItemModel != &aItemModel)
			{
				neolib::scoped_flag sf{ iInitializing };
				if (has_item_model())
					item_model().unsubscribe(*this);
				iItemModel = &aItemModel;
				item_model().subscribe(*this);
				iColumns.clear();
				for (item_model_index::column_type col = 0; col < item_model().columns(); ++col)
					iColumns.push_back(column_info{ col });
				iRows.clear();
				for (item_model_index::row_type row = 0; row < item_model().rows(); ++row)
					item_added(item_model(), item_model_index{ row });
				reset_maps();
				reset_meta();
				reset_sort();
			}
			notify_observers(i_item_presentation_model_subscriber::NotifyItemModelChanged);
		}
		item_model_index to_item_model_index(const item_presentation_model_index& aIndex) const override
		{
			return item_model_index{ iRows[aIndex.row()].first, iColumns[aIndex.column()].modelColumn };
		}
		bool have_item_model_index(const item_model_index& aIndex) const override
		{
			return row_map().find(aIndex.row()) != row_map().end() && column_map().find(aIndex.column()) != column_map().end();
		}
		item_presentation_model_index from_item_model_index(const item_model_index& aIndex) const override
		{
			auto index = std::make_pair(row_map().find(aIndex.row()), column_map().find(aIndex.column()));
			if (index.first == row_map().end() || index.second == column_map().end())
				throw bad_item_model_index();
			return item_presentation_model_index{ index.first->second, index.second->second };
		}
	public:
		uint32_t rows() const override
		{
			return iRows.size();
		} 
		uint32_t columns() const override
		{
			return iColumns.size();
		}
		uint32_t columns(const item_presentation_model_index& aIndex) const override
		{
			return iRows[aIndex.row()].second.size();
		}
		dimension column_width(item_presentation_model_index::value_type aColumnIndex, const graphics_context& aGraphicsContext, bool aIncludeMargins = true) const override
		{
			if (iColumns.size() < aColumnIndex + 1)
				return 0.0;
			auto& columnWidth = iColumns[aColumnIndex].width;
			if (columnWidth != boost::none)
				return *columnWidth + (aIncludeMargins ? cell_margins(aGraphicsContext).size().cx : 0.0);
			columnWidth = 0.0;
			for (item_presentation_model_index::row_type row = 0; row < iRows.size(); ++row)
			{
				auto modelIndex = to_item_model_index(item_presentation_model_index{ row, aColumnIndex });
				if (modelIndex.column() >= item_model().columns(modelIndex))
					continue;
				auto cellWidth = cell_extents(item_presentation_model_index{ row, aColumnIndex }, aGraphicsContext).cx;
				columnWidth = std::max(*columnWidth, cellWidth);
			}
			return *columnWidth + (aIncludeMargins ? cell_margins(aGraphicsContext).size().cx : 0.0);
		}
		const std::string& column_heading_text(item_presentation_model_index::column_type aColumnIndex) const override
		{
			if (iColumns.size() < aColumnIndex + 1)
				throw bad_column_index();
			if (iColumns[aColumnIndex].headingText != boost::none)
				return *iColumns[aColumnIndex].headingText;
			else
				return item_model().column_name(iColumns[aColumnIndex].modelColumn);
		}
		size column_heading_extents(item_presentation_model_index::column_type aColumnIndex, const graphics_context& aGraphicsContext) const override
		{
			if (iColumns.size() < aColumnIndex + 1)
				throw bad_column_index();
			if (iColumns[aColumnIndex].headingFont != font{})
			{
				iColumns[aColumnIndex].headingFont = font{};
				iColumns[aColumnIndex].headingExtents = boost::none;
			}
			if (iColumns[aColumnIndex].headingExtents != boost::none)
				return units_converter(aGraphicsContext).from_device_units(*iColumns[aColumnIndex].headingExtents);
			size columnHeadingExtents = aGraphicsContext.multiline_text_extent(column_heading_text(aColumnIndex), iColumns[aColumnIndex].headingFont);
			iColumns[aColumnIndex].headingExtents = units_converter(aGraphicsContext).to_device_units(columnHeadingExtents);
			iColumns[aColumnIndex].headingExtents->cx = std::ceil(iColumns[aColumnIndex].headingExtents->cx);
			iColumns[aColumnIndex].headingExtents->cy = std::ceil(iColumns[aColumnIndex].headingExtents->cy);
			return units_converter(aGraphicsContext).from_device_units(*iColumns[aColumnIndex].headingExtents);
		}
		void set_column_heading_text(item_presentation_model_index::column_type aColumnIndex, const std::string& aHeadingText) override
		{
			if (iColumns.size() < aColumnIndex + 1)
				throw bad_column_index();
			iColumns[aColumnIndex].headingText = aHeadingText;
			iColumns[aColumnIndex].headingExtents = boost::none;
			notify_observers(i_item_presentation_model_subscriber::NotifyColumnInfoChanged, aColumnIndex);
		}
		item_cell_editable column_editable(item_presentation_model_index::value_type aColumnIndex) const override
		{
			if (iColumns.size() < aColumnIndex + 1)
				throw bad_column_index();
			return iColumns[aColumnIndex].editable;
		}
		void set_column_editable(item_presentation_model_index::value_type aColumnIndex, item_cell_editable aEditable) override
		{
			if (iColumns.size() < aColumnIndex + 1)
				throw bad_column_index();
			iColumns[aColumnIndex].editable = aEditable;
		}
		const font& default_font() const override
		{
			if (iDefaultFont != boost::none)
				return *iDefaultFont;
			return app::instance().current_style().font();
		}
		void set_default_font(const optional_font& aDefaultFont) override
		{
			if (iDefaultFont != aDefaultFont)
			{
				iDefaultFont = aDefaultFont;
				reset_meta();
			}
		}
		size cell_spacing(const i_units_context& aUnitsContext) const override
		{
			if (iCellSpacing == boost::none)
			{
				dimension millimetre = as_units(aUnitsContext, units::Millimetres, 1.0);
				basic_size<int> result = units_converter(aUnitsContext).from_device_units(units_converter(aUnitsContext).to_device_units(size{ millimetre, millimetre }).ceil());
				if (result.cx % 2 == 1)
					++result.cx;
				if (result.cy % 2 == 1)
					++result.cy;
				return result;
			}
			return units_converter(aUnitsContext).from_device_units(*iCellSpacing);
		}
		void set_cell_spacing(const optional_size& aSpacing, const i_units_context& aUnitsContext) override
		{
			if (aSpacing == boost::none)
				iCellSpacing = aSpacing;
			else
				iCellSpacing = units_converter(aUnitsContext).to_device_units(*aSpacing);
		}
		neogfx::margins cell_margins(const i_units_context& aUnitsContext) const override
		{
			if (iCellMargins == boost::none)
			{
				return units_converter(aUnitsContext).from_device_units(neogfx::margins{ 1.0, 1.0, 1.0, 1.0 });
			}
			return units_converter(aUnitsContext).from_device_units(*iCellMargins);
		}
		void set_cell_margins(const optional_margins& aMargins, const i_units_context& aUnitsContext) override
		{
			if (aMargins == boost::none)
				iCellMargins = aMargins;
			else
				iCellMargins = units_converter(aUnitsContext).to_device_units(*aMargins);
		}
	public:
		dimension item_height(const item_presentation_model_index& aIndex, const i_units_context& aUnitsContext) const override
		{
			dimension height = 0.0;
			for (uint32_t col = 0; col < iRows[aIndex.row()].second.size(); ++col)
			{
				auto modelIndex = to_item_model_index(item_presentation_model_index{ aIndex.row(), col });
				if (modelIndex.column() >= item_model().columns(modelIndex))
					continue;
				optional_font cellFont = cell_font(aIndex);
				if (cell_meta(aIndex).extents != boost::none)
					height = std::max(height, units_converter(aUnitsContext).from_device_units(*cell_meta(aIndex).extents).cy);
				else
				{
					std::string cellString = cell_to_string(item_presentation_model_index(aIndex.row(), col));
					const font& effectiveFont = (cellFont == boost::none ? default_font() : *cellFont);
					height = std::max(height, units_converter(aUnitsContext).from_device_units(size(0.0, std::ceil(effectiveFont.height()))).cy *
						(1 + std::count(cellString.begin(), cellString.end(), '\n')));
				}
			}
			return height + cell_margins(aUnitsContext).size().cy + cell_spacing(aUnitsContext).cy;
		}
		double total_height(const i_units_context& aUnitsContext) const override
		{
			if (iTotalHeight != boost::none)
				return *iTotalHeight;
			i_scrollbar::value_type height = 0.0;
			for (item_presentation_model_index::row_type row = 0; row < iRows.size(); ++row)
				height += item_height(item_presentation_model_index(row, 0), aUnitsContext);
			iTotalHeight = height;
			return *iTotalHeight;
		}
		double item_position(const item_presentation_model_index& aIndex, const i_units_context& aUnitsContext) const override
		{
			if (iPositions[aIndex.row()] == boost::none)
			{
				auto pred = [](const optional_position& lhs, const optional_position& rhs) -> bool
				{
					if (lhs == boost::none && rhs == boost::none)
						return false;
					else if (lhs != boost::none && rhs == boost::none)
						return true;
					else if (lhs == boost::none && rhs != boost::none)
						return false;
					else
						return *lhs < *rhs;
				};
				auto i = std::lower_bound(iPositions.begin(), iPositions.end(), optional_position(), pred);
				uint32_t row = std::distance(iPositions.begin(), i);
				double position = (row == 0 ? 0.0 : **(std::prev(i)) + item_height(item_presentation_model_index(row - 1), aUnitsContext));
				while (row <= aIndex.row())
				{
					iPositions[row] = position;
					position += item_height(item_presentation_model_index(row), aUnitsContext);
					++row;
				}
			}
			return *iPositions[aIndex.row()];
		}
		std::pair<item_presentation_model_index::row_type, coordinate> item_at(double aPosition, const i_units_context& aUnitsContext) const override
		{
			if (iRows.size() == 0)
				return std::pair<item_presentation_model_index::row_type, coordinate>(0, 0.0);
			auto pred = [](const optional_position& lhs, const optional_position& rhs) -> bool
			{
				if (lhs == boost::none && rhs == boost::none)
					return false;
				else if (lhs != boost::none && rhs == boost::none)
					return true;
				else if (lhs == boost::none && rhs != boost::none)
					return false;
				else
					return *lhs < *rhs;
			};
			auto i = std::lower_bound(iPositions.begin(), iPositions.end(), aPosition, pred);
			if ((i == iPositions.end() || (*i != boost::none && **i > aPosition)) && i != iPositions.begin())
				--i;
			while (*i == boost::none)
			{
				auto j = std::lower_bound(iPositions.begin(), iPositions.end(), optional_position(), pred);
				uint32_t row = std::distance(iPositions.begin(), j);
				double position = (row == 0 ? 0.0 : **(std::prev(j)) + item_height(item_presentation_model_index(row - 1), aUnitsContext));
				while (row < iPositions.size())
				{
					iPositions[row] = position;
					position += item_height(item_presentation_model_index(row), aUnitsContext);
					++row;
				}
				i = std::lower_bound(iPositions.begin(), iPositions.end(), aPosition, pred);
				if ((i == iPositions.end() || (*i != boost::none && **i > aPosition)) && i != iPositions.begin())
					--i;
			}
			return std::pair<item_presentation_model_index::row_type, coordinate>(std::distance(iPositions.begin(), i), static_cast<coordinate>(**i - aPosition));
		}
	public:
		const cell_meta_type& cell_meta(const item_presentation_model_index& aIndex) const override
		{
			return iRows[aIndex.row()].second[aIndex.column()];
		}
	public:
		item_cell_editable cell_editable(const item_presentation_model_index& aIndex) const override
		{
			if (!item_model().cell_data_info(to_item_model_index(aIndex)).readOnly)
				return column_editable(aIndex.column());
			else
				return item_cell_editable::No;
		}
		std::string cell_to_string(const item_presentation_model_index& aIndex) const override
		{
			auto modelIndex = to_item_model_index(aIndex);
			switch (item_model().cell_data(modelIndex).which())
			{
			case 0:
				return "";
			case item_cell_data::type_id<bool>::value:
				return (cell_format(aIndex) % static_variant_cast<bool>(item_model().cell_data(modelIndex))).str();
			case item_cell_data::type_id<int32_t>::value:
				return (cell_format(aIndex) % static_variant_cast<int32_t>(item_model().cell_data(modelIndex))).str();
			case item_cell_data::type_id<uint32_t>::value:
				return (cell_format(aIndex) % static_variant_cast<uint32_t>(item_model().cell_data(modelIndex))).str();
			case item_cell_data::type_id<int64_t>::value:
				return (cell_format(aIndex) % static_variant_cast<int64_t>(item_model().cell_data(modelIndex))).str();
			case item_cell_data::type_id<uint64_t>::value:
				return (cell_format(aIndex) % static_variant_cast<uint64_t>(item_model().cell_data(modelIndex))).str();
			case item_cell_data::type_id<float>::value:
				return (cell_format(aIndex) % static_variant_cast<float>(item_model().cell_data(modelIndex))).str();
			case item_cell_data::type_id<double>::value:
				return (cell_format(aIndex) % static_variant_cast<double>(item_model().cell_data(modelIndex))).str();
			case item_cell_data::type_id<std::string>::value:
				return (cell_format(aIndex) % static_variant_cast<const std::string&>(item_model().cell_data(modelIndex))).str();
			default:
				return "";
			}
		}
		item_cell_data string_to_cell_data(const item_presentation_model_index& aIndex, const std::string& aString) const override
		{
			bool error = false;
			return string_to_cell_data(aIndex, aString, error);
		}
		item_cell_data string_to_cell_data(const item_presentation_model_index& aIndex, const std::string& aString, bool& aError) const override
		{
			aError = false;
			auto const& cellDataInfo = item_model().cell_data_info(to_item_model_index(aIndex));
			std::istringstream input{ aString };
			std::string guff;
			switch (cellDataInfo.type)
			{
			case item_cell_data_type::Bool:
				{
					bool value;
					if (!(input >> value) || (input >> guff))
					{
						aError = true;
						return has_item_model() ? item_model().cell_data(to_item_model_index(aIndex)) : item_cell_data{};
					}
					return value;
				}
			case item_cell_data_type::Int32:
				{
					int32_t value;
					if (!(input >> value) || (input >> guff))
					{
						aError = true;
						return has_item_model() ? item_model().cell_data(to_item_model_index(aIndex)) : item_cell_data{};
					}
					if (cellDataInfo.min != boost::none)
						value = std::max(value, static_variant_cast<int32_t>(cellDataInfo.min));
					if (cellDataInfo.max != boost::none)
						value = std::min(value, static_variant_cast<int32_t>(cellDataInfo.max));
					return value;
				}
			case item_cell_data_type::UInt32:
				{
					uint32_t value;
					if (!(input >> value) || (input >> guff))
					{
						aError = true;
						return has_item_model() ? item_model().cell_data(to_item_model_index(aIndex)) : item_cell_data{};
					}
					if (cellDataInfo.min != boost::none)
						value = std::max(value, static_variant_cast<uint32_t>(cellDataInfo.min));
					if (cellDataInfo.max != boost::none)
						value = std::min(value, static_variant_cast<uint32_t>(cellDataInfo.max));
					return value;
				}
			case item_cell_data_type::Int64:
				{
					int64_t value;
					if (!(input >> value) || (input >> guff))
					{
						aError = true;
						return has_item_model() ? item_model().cell_data(to_item_model_index(aIndex)) : item_cell_data{};
					}
					if (cellDataInfo.min != boost::none)
						value = std::max(value, static_variant_cast<int64_t>(cellDataInfo.min));
					if (cellDataInfo.max != boost::none)
						value = std::min(value, static_variant_cast<int64_t>(cellDataInfo.max));
					return value;
				}
			case item_cell_data_type::UInt64:
				{
					uint64_t value;
					if (!(input >> value) || (input >> guff))
					{
						aError = true;
						return has_item_model() ? item_model().cell_data(to_item_model_index(aIndex)) : item_cell_data{};
					}
					if (cellDataInfo.min != boost::none)
						value = std::max(value, static_variant_cast<uint64_t>(cellDataInfo.min));
					if (cellDataInfo.max != boost::none)
						value = std::min(value, static_variant_cast<uint64_t>(cellDataInfo.max));
					return value;
				}
			case item_cell_data_type::Float:
				{
					float value;
					if (!(input >> value) || (input >> guff))
					{
						aError = true;
						return has_item_model() ? item_model().cell_data(to_item_model_index(aIndex)) : item_cell_data{};
					}
					if (cellDataInfo.min != boost::none)
						value = std::max(value, static_variant_cast<float>(cellDataInfo.min));
					if (cellDataInfo.max != boost::none)
						value = std::min(value, static_variant_cast<float>(cellDataInfo.max));
					return value;
				}
			case item_cell_data_type::Double:
				{
					double value;
					if (!(input >> value) || (input >> guff))
					{
						aError = true;
						return has_item_model() ? item_model().cell_data(to_item_model_index(aIndex)) : item_cell_data{};
					}
					if (cellDataInfo.min != boost::none)
						value = std::max(value, static_variant_cast<double>(cellDataInfo.min));
					if (cellDataInfo.max != boost::none)
						value = std::min(value, static_variant_cast<double>(cellDataInfo.max));
					return value;
				}
			case item_cell_data_type::String:
			default:
				return aString;
			}
		}
		boost::basic_format<char> cell_format(const item_presentation_model_index&) const override
		{
			static const boost::basic_format<char> sDefaultFormat("%1%");
			return sDefaultFormat;
		}
		optional_colour cell_colour(const item_presentation_model_index&, item_cell_colour_type) const override
		{
			return optional_colour{};
		}
		optional_font cell_font(const item_presentation_model_index&) const override
		{
			return optional_font{};
		}
		neogfx::glyph_text& cell_glyph_text(const item_presentation_model_index& aIndex, const graphics_context& aGraphicsContext) const override
		{
			optional_font cellFont = cell_font(aIndex);
			if (cell_meta(aIndex).text != boost::none)
				return *cell_meta(aIndex).text;
			cell_meta(aIndex).text = aGraphicsContext.to_glyph_text(cell_to_string(aIndex), cellFont == boost::none ? default_font() : *cellFont);
			return *cell_meta(aIndex).text;
		}
		size cell_extents(const item_presentation_model_index& aIndex, const graphics_context& aGraphicsContext) const override
		{
			auto oldItemHeight = item_height(aIndex, aGraphicsContext);
			optional_font cellFont = cell_font(aIndex);
			if (cell_meta(aIndex).extents != boost::none)
				return units_converter(aGraphicsContext).from_device_units(*cell_meta(aIndex).extents);
			size cellExtents = cell_glyph_text(aIndex, aGraphicsContext).extents();
			if (cellExtents.cy == 0.0)
				cellExtents.cy = (cellFont == boost::none ? default_font() : *cellFont).height();
			cell_meta(aIndex).extents = units_converter(aGraphicsContext).to_device_units(cellExtents);
			cell_meta(aIndex).extents->cx = std::ceil(cell_meta(aIndex).extents->cx);
			cell_meta(aIndex).extents->cy = std::ceil(cell_meta(aIndex).extents->cy);
			if (iTotalHeight != boost::none)
				*iTotalHeight += (item_height(aIndex, aGraphicsContext) - oldItemHeight);
			return units_converter(aGraphicsContext).from_device_units(*cell_meta(aIndex).extents);
		}
	public:
		optional_sort sorting_by() const override
		{
			if (!iSortOrder.empty())
				return iSortOrder.front();
			else
				return optional_sort{};
		}
		void sort_by(item_presentation_model_index::column_type aColumnIndex, const optional_sort_direction& aSortDirection = optional_sort_direction{}) override
		{
			iSortOrder.push_front(sort{ aColumnIndex, aSortDirection == boost::none ? SortAscending : *aSortDirection });
			for (auto i = std::next(iSortOrder.begin()); i != iSortOrder.end(); ++i)
			{
				if (i->first == aColumnIndex)
				{
					if (aSortDirection == boost::none)
					{
						if (i == std::next(iSortOrder.begin()))
							iSortOrder.front().second = (i->second == SortAscending ? SortDescending : SortAscending);
						else
							iSortOrder.front().second = i->second;
					}
					iSortOrder.erase(i);
					break;
				}
			}
			execute_sort();
		}
		void reset_sort() override
		{
			iSortOrder.clear();
			execute_sort();
		}
	public:
		optional_item_presentation_model_index find_item(const filter_search_key& aFilterSearchKey, item_presentation_model_index::column_type aColumnIndex = 0, filter_search_type_e aFilterSearchType = Prefix, case_sensitivity_e aCaseSensitivity = CaseInsensitive) const override
		{
			for (item_presentation_model_index::row_type row = 0; row < iRows.size(); ++row)
			{
				auto modelIndex = to_item_model_index(item_presentation_model_index{ row, aColumnIndex });
				const auto& origValue = item_model().cell_data(modelIndex).to_string();
				const auto& value = aCaseSensitivity == CaseSensitive ? origValue : boost::to_upper_copy<std::string>(origValue);
				const auto& origKey = aFilterSearchKey;
				const auto& key = aCaseSensitivity == CaseSensitive ? origKey : boost::to_upper_copy<std::string>(origKey);
				if (key.empty())
					continue;
				switch (aFilterSearchType)
				{
				case Prefix:
					if (value.size() >= key.size() && value.substr(0, key.size()) == key)
						return from_item_model_index(modelIndex);
				}
			}
			return optional_item_presentation_model_index{};
		}
	public:
		bool filtering() const override
		{
			return iFiltering;
		}
		optional_filter filtering_by() const override
		{
			if (!iFilters.empty())
				return iFilters.front();
			else
				return optional_filter{};
		}
		void filter_by(item_presentation_model_index::column_type aColumnIndex, const filter_search_key& aFilterSearchKey, filter_search_type_e aFilterSearchType = Value, case_sensitivity_e aCaseSensitivity = CaseInsensitive) override
		{
			iFilters.push_back(filter{ aColumnIndex, aFilterSearchKey, aFilterSearchType, aCaseSensitivity });
			for (auto i = iFilters.begin(); i != std::prev(iFilters.end()); ++i)
			{
				if (std::get<0>(*i) == aColumnIndex)
				{
					iFilters.erase(i);
					break;
				}
			}			
			execute_filter();
		}
		void reset_filter() override
		{
			if (!iFilters.empty())
			{
				iFilters.clear();
				execute_filter();
			}
		}
	public:
		virtual void subscribe(i_item_presentation_model_subscriber& aSubscriber)
		{
			add_observer(aSubscriber);
		}
		virtual void unsubscribe(i_item_presentation_model_subscriber& aSubscriber)
		{
			remove_observer(aSubscriber);
		}
	private:
		void init()
		{
			iSink = app::instance().rendering_engine().subpixel_rendering_changed([this]()
			{
				reset_meta();
			});
			iSink += app::instance().current_style_changed([this](style_aspect aAspect)
			{
				if ((aAspect & (style_aspect::Geometry | style_aspect::Font)) != style_aspect::None)
					reset_meta();
			});
			reset_sort();
		}
		void execute_sort()
		{
			notify_observers(i_item_presentation_model_subscriber::NotifyItemsSorting);
			std::sort(iRows.begin(), iRows.end(), [&](const typename container_type::value_type& aLhs, const typename container_type::value_type& aRhs) -> bool
			{
				if (!iSortOrder.empty())
				{
					for (std::size_t i = 0; i < iSortOrder.size(); ++i)
					{
						auto col = iSortOrder[i].first;
						const auto& v1 = item_model().cell_data(item_model_index{ aLhs.first, iColumns[col].modelColumn });
						const auto& v2 = item_model().cell_data(item_model_index{ aRhs.first, iColumns[col].modelColumn });
						if (v1.is<std::string>() && v2.is<std::string>())
						{
							std::string s1 = boost::to_upper_copy<std::string>(v1);
							std::string s2 = boost::to_upper_copy<std::string>(v2);
							if (s1 < s2)
								return iSortOrder[i].second == SortAscending;
							else if (s2 < s1)
								return iSortOrder[i].second == SortDescending;
						}
						if (v1 < v2)
							return iSortOrder[i].second == SortAscending;
						else if (v2 < v1)
							return iSortOrder[i].second == SortDescending;
					}
					return false;
				}
				else
					return aLhs.first < aRhs.first;
			});
			reset_maps();
			reset_position_meta(0);
			notify_observers(i_item_presentation_model_subscriber::NotifyItemsSorted);
		}
		void execute_filter()
		{
			neolib::scoped_flag sf1{ iInitializing };
			neolib::scoped_flag sf2{ iFiltering };
			notify_observers(i_item_presentation_model_subscriber::NotifyItemsFiltering);
			iRows.clear();
			for (item_model_index::row_type row = 0; row < item_model().rows(); ++row)
			{
				bool matches = true;
				for (const auto& filter : iFilters)
				{
					const auto& origValue = item_model().cell_data(item_model_index{ row, iColumns[std::get<0>(filter)].modelColumn }).to_string();
					const auto& value = (std::get<3>(filter) == CaseSensitive ? origValue : boost::to_upper_copy<std::string>(origValue));
					const auto& origKey = std::get<1>(filter);
					const auto& key = (std::get<3>(filter) == CaseSensitive ? origKey : boost::to_upper_copy<std::string>(origKey));
					if (key.empty())
						continue;
					switch (std::get<2>(filter))
					{
					case Prefix:
						if (value.size() < key.size() || value.substr(0, key.size()) != key)
							matches = false;
						break;
					case Glob:
						// todo
						break;
					case Regex:
						// todo
						break;
					}
				}
				if (matches)
					item_added(item_model(), item_model_index{ row });
			}
			reset_maps();
			reset_cell_meta();
			reset_position_meta(0);
			notify_observers(i_item_presentation_model_subscriber::NotifyItemsFiltered);
			execute_sort();
		}
	private:
		void column_info_changed(const i_item_model&, item_model_index::column_type aColumnIndex) override
		{
			reset_column_meta();
			if (have_item_model_index(item_model_index{ 0, aColumnIndex }))
				notify_observers(i_item_presentation_model_subscriber::NotifyColumnInfoChanged, from_item_model_index(item_model_index{ 0, aColumnIndex }).column());
		}
		void item_added(const i_item_model& aItemModel, const item_model_index& aItemIndex) override
		{
			for (auto& row : iRows)
				if (row.first >= aItemIndex.row())
					++row.first;
			iRows.push_back(std::make_pair(aItemIndex.row(), row_container_type{ aItemModel.columns() }));
			if (!iInitializing)
			{
				notify_observers(i_item_presentation_model_subscriber::NotifyItemAdded, item_presentation_model_index{ iRows.size() - 1, 0 });
				reset_maps();
				reset_position_meta(0);
				execute_sort();
			}
		}
		void item_changed(const i_item_model&, const item_model_index& aItemIndex) override
		{
			if (!iInitializing)
			{
				bool newColumns = false;
				for (item_model_index::column_type col = iColumns.size(); col < item_model().columns(); ++col)
				{
					iColumns.push_back(column_info{ col });
					newColumns = true;
				}
				if (newColumns)
					for (auto& row : iRows)
						row.second.resize(item_model().columns());
				auto& cellMeta = cell_meta(from_item_model_index(aItemIndex));
				cellMeta.text = boost::none;
				cellMeta.extents = boost::none;
				notify_observers(i_item_presentation_model_subscriber::NotifyItemChanged, from_item_model_index(aItemIndex));
				iColumns[aItemIndex.column()].width = boost::none;
				reset_maps();
				reset_position_meta(0);
				execute_sort();
			}
		}
		void item_removed(const i_item_model&, const item_model_index& aItemIndex) override
		{
			if (!iInitializing)
				notify_observers(i_item_presentation_model_subscriber::NotifyItemRemoved, from_item_model_index(aItemIndex));
			iRows.erase(iRows.begin() + from_item_model_index(aItemIndex).row());
			for (auto& row : iRows)
				if (row.first >= aItemIndex.row())
					--row.first;
			reset_maps();
			reset_position_meta(0);
		}
		void model_destroyed(const i_item_model&) override
		{
			iItemModel = 0;
		}
	private:
		void reset_maps() const
		{
			iRowMap.clear();
			iColumnMap.clear();
		}
		const row_map_type& row_map() const
		{
			if (iRowMap.empty())
				for (item_presentation_model_index::row_type row = 0; row < iRows.size(); ++row)
					iRowMap[iRows[row].first] = row;
			return iRowMap;
		}
		row_map_type& row_map()
		{
			return const_cast<row_map_type&>(const_cast<const basic_item_presentation_model&>(*this).row_map()):
		}
		const column_map_type& column_map() const
		{
			if (iColumnMap.empty())
				for (item_presentation_model_index::column_type col = 0; col < iColumns.size(); ++col)
					iColumnMap[iColumns[col].modelColumn] = col;
			return iColumnMap;
		}
		column_map_type& column_map()
		{
			return const_cast<column_map_type&>(const_cast<const basic_item_presentation_model&>(*this).column_map()) :
		}
		void reset_meta() const
		{
			reset_cell_meta();
			reset_column_meta();
			reset_position_meta(0);
		}
		void reset_cell_meta() const
		{
			for (uint32_t row = 0; row < iRows.size(); ++row)
			{
				for (uint32_t col = 0; col < iColumns.size(); ++col)
				{
					cell_meta(item_presentation_model_index(row, col)).text = boost::none;
					cell_meta(item_presentation_model_index(row, col)).extents = boost::none;
				}
			}
		}
		void reset_column_meta() const
		{
			for (uint32_t col = 0; col < iColumns.size(); ++col)
			{
				iColumns[col].width = boost::none;
				iColumns[col].headingExtents = boost::none;
			}
		}
		void reset_position_meta(item_presentation_model_index::row_type aFromRow) const
		{
			iTotalHeight = boost::none;
			iPositions.resize(iRows.size());
			for (std::size_t i = aFromRow; i < iPositions.size(); ++i)
				iPositions[i] = boost::none;
		}
	private:
		void notify_observer(i_item_presentation_model_subscriber& aObserver, i_item_presentation_model_subscriber::notify_type aType, const void* aParameter, const void*) override
		{
			switch (aType)
			{
			case i_item_presentation_model_subscriber::NotifyColumnInfoChanged:
				aObserver.column_info_changed(*this, *static_cast<const item_presentation_model_index::value_type*>(aParameter));
				break;
			case i_item_presentation_model_subscriber::NotifyItemModelChanged:
				aObserver.item_model_changed(*this, item_model());
				break;
			case i_item_presentation_model_subscriber::NotifyItemAdded:
				aObserver.item_added(*this, *static_cast<const item_presentation_model_index*>(aParameter));
				break;
			case i_item_presentation_model_subscriber::NotifyItemChanged:
				aObserver.item_changed(*this, *static_cast<const item_presentation_model_index*>(aParameter));
				break;
			case i_item_presentation_model_subscriber::NotifyItemRemoved:
				aObserver.item_removed(*this, *static_cast<const item_presentation_model_index*>(aParameter));
				break;
			case i_item_presentation_model_subscriber::NotifyItemsSorting:
				aObserver.items_sorting(*this);
				break;
			case i_item_presentation_model_subscriber::NotifyItemsSorted:
				aObserver.items_sorted(*this);
				break;
			case i_item_presentation_model_subscriber::NotifyItemsFiltering:
				aObserver.items_filtering(*this);
				break;
			case i_item_presentation_model_subscriber::NotifyItemsFiltered:
				aObserver.items_filtered(*this);
				break;
			case i_item_presentation_model_subscriber::NotifyModelDestroyed:
				aObserver.model_destroyed(*this);
				break;
			}
		}
	private:
		i_item_model* iItemModel;
		optional_size iCellSpacing;
		optional_margins iCellMargins;
		container_type iRows;
		mutable row_map_type iRowMap;
		column_info_container_type iColumns;
		mutable column_map_type iColumnMap;
		mutable optional_font iDefaultFont;
		mutable boost::optional<i_scrollbar::value_type> iTotalHeight;
		mutable neolib::segmented_array<optional_position, 256> iPositions;
		std::deque<sort> iSortOrder;
		std::vector<filter> iFilters;
		sink iSink;
		bool iInitializing;
		bool iFiltering;
	};

	typedef basic_item_presentation_model<item_model> item_presentation_model;
}