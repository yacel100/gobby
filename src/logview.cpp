/* gobby - A GTKmm driven libobby client
 * Copyright (C) 2005 0x539 dev group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <ctime>
#include <sstream>
#include "logview.hpp"

Gobby::LogView::LogView()
 : Gtk::TextView()
{
	m_end_mark = get_buffer()->create_mark(
		"end_mark", get_buffer()->end(), false
	);

	set_editable(false);
}

Gobby::LogView::~LogView()
{
}

void Gobby::LogView::clear()
{
	get_buffer()->set_text("");
}

void Gobby::LogView::log(const Glib::ustring& text, const Glib::ustring& color)
{
	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();
	Glib::RefPtr<Gtk::TextTag> tag = buffer->get_tag_table()->lookup(color);

	Glib::ustring ins_text = text;
	if(ins_text[ins_text.length() - 1] != '\n') ins_text += "\n";

	struct std::tm* cur_time;
	time_t cur_time_t = std::time(NULL);
	cur_time = localtime(&cur_time_t);

	std::stringstream timestream;
	timestream << "[";
	timestream.width(2); timestream.fill('0');
	timestream << cur_time->tm_hour;
	timestream << ":";
	timestream.width(2); timestream.fill('0');
	timestream << cur_time->tm_min;
	timestream << ":";
	timestream.width(2); timestream.fill('0');
	timestream << cur_time->tm_sec;
	timestream << "] ";
	ins_text = timestream.str() + ins_text;

	if(!tag)
	{
		tag = Gtk::TextTag::create();
		tag->property_foreground() = color;
		buffer->get_tag_table()->add(tag);
	}

	buffer->insert_with_tag(buffer->end(), ins_text, tag);

	scroll_to_mark(m_end_mark, 0.0f);
}

