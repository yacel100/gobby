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

#include <iostream>
#include <obby/format_string.hpp>
#include <obby/user_table.hpp>
#include <obby/client_document.hpp>
#include <obby/host_document.hpp>
#include <obby/client_buffer.hpp>
#include <obby/host_buffer.hpp>
#include "common.hpp"
#include "document.hpp"
#include "folder.hpp"

Gobby::Document::Document(obby::local_document_info& doc, const Folder& folder,
                          const Preferences& preferences)
 : Gtk::SourceView(),
   m_doc(doc), m_folder(folder), m_subscribed(false),
   m_preferences(preferences), m_editing(true),
   m_btn_subscribe(_("Subscribe") ), m_title(doc.get_title() )
{
	Glib::RefPtr<Gtk::SourceBuffer> buf = get_buffer();

	// Prevent from GTK sourceview's undo 
	buf->begin_not_undoable_action();

	// Set monospaced font
	Pango::FontDescription desc;
	desc.set_family("monospace");
	modify_font(desc);

	// Set SourceLanguage by file extension
	Glib::ustring mime_type =
		folder.get_mime_map().get_mime_type_by_file(doc.get_title() );
	if(!mime_type.empty() )
	{
		Glib::RefPtr<Gtk::SourceLanguagesManager> manager =
			folder.get_lang_manager();
		Glib::RefPtr<Gtk::SourceLanguage> language = 
			manager->get_language_from_mime_type(mime_type);

		if(language)
			buf->set_language(language);
	}

	// Insert user tags into the tag table
	const obby::user_table& user_table = doc.get_buffer().get_user_table();
	for(obby::user_table::user_iterator<obby::user::NONE> iter =
		user_table.user_begin<obby::user::NONE>();
	    iter != user_table.user_end<obby::user::NONE>();
	    ++ iter)
	{
		// Create new tag
		Glib::RefPtr<Gtk::TextBuffer::Tag> tag =
			buf->create_tag("gobby_user_" + iter->get_name() );

		// Build user color
		Gdk::Color color;
		color.set_red(iter->get_red() * 65535 / 255);
		color.set_green(iter->get_green() * 65535 / 255);
		color.set_blue(iter->get_blue() * 65535 / 255);

		// Assign color to tag
		tag->property_background_gdk() = color;
	}

	// Textbuffer signal handlers
	buf->signal_insert().connect(
		sigc::mem_fun(*this, &Document::on_insert_before), false);
	buf->signal_erase().connect(
		sigc::mem_fun(*this, &Document::on_erase_before), false);
	buf->signal_insert().connect(
		sigc::mem_fun(*this, &Document::on_insert_after), true);
	buf->signal_erase().connect(
		sigc::mem_fun(*this, &Document::on_erase_after), true);
	buf->signal_mark_set().connect(
		sigc::mem_fun(*this, &Document::on_mark_set), false);
	buf->signal_apply_tag().connect(
		sigc::mem_fun(*this, &Document::on_apply_tag_after), true);

	// Obby signal handlers
	doc.subscribe_event().connect(
		sigc::mem_fun(*this, &Document::on_obby_user_subscribe) );
	doc.unsubscribe_event().connect(
		sigc::mem_fun(*this, &Document::on_obby_user_unsubscribe) );

#if 0
	// Allow drag+drop of uri-lists and plaintext. uri-list is forwarded to
	// the window while plaintext will be inserted into the document.
	std::list<Gtk::TargetEntry> targets;
	targets.push_back(Gtk::TargetEntry("text/uri-list") );
	targets.push_back(Gtk::TargetEntry("text/plain") );
	drag_dest_set(targets);
#endif

	// GUI signal handlers
	m_btn_subscribe.signal_clicked().connect(
		sigc::mem_fun(*this, &Document::on_gui_subscribe) );

	// Apply preferences
	apply_preferences();

	// Set introduction text
	set_intro_text();

	m_editing = false;
}

Gobby::Document::~Document()
{
}

const obby::local_document_info& Gobby::Document::get_document() const
{
	return m_doc;
}

obby::local_document_info& Gobby::Document::get_document()
{
	return m_doc;
}

Gobby::Document::signal_cursor_moved_type
Gobby::Document::cursor_moved_event() const
{
	return m_signal_cursor_moved;
}

Gobby::Document::signal_content_changed_type
Gobby::Document::content_changed_event() const
{
	return m_signal_content_changed;
}

Gobby::Document::signal_language_changed_type
Gobby::Document::language_changed_event() const
{
	return m_signal_language_changed;
}

void Gobby::Document::get_cursor_position(unsigned int& row,
                                          unsigned int& col)
{
	// Get insert mark
	Glib::RefPtr<Gtk::TextBuffer::Mark> mark =
		get_buffer()->get_insert();

	// Get corresponding iterator
	// Gtk::TextBuffer::Mark::get_iter is not const. Why not? It prevents
	// this function from being const.
	const Gtk::TextBuffer::iterator iter = mark->get_iter();

	// Read line and column from iterator
	row = iter.get_line();
	col = iter.get_line_offset();
}

unsigned int Gobby::Document::get_unsynced_changes_count() const
{
	// Get document
	obby::local_document* local_doc = m_doc.get_document();
	// No document? Seems that we are not subscribed
	if(!local_doc) return 0;
	// Cast to client document
	obby::client_document* client_doc = 
		dynamic_cast<obby::client_document*>(local_doc);
	// No client document? Host is always synced
	if(!client_doc) return 0;
	// Document returns amount otherwise
	return client_doc->unsynced_count();
}

unsigned int Gobby::Document::get_revision() const
{
	// Get document
	obby::local_document* local_doc = m_doc.get_document();
	// No document? Seems that we are not subscribed (-> no revision)
	if(!local_doc) return 0;
	// Get revision from obby document
	return local_doc->get_revision();
}

const Glib::ustring& Gobby::Document::get_title() const
{
	return m_title;
}

const Glib::ustring& Gobby::Document::get_path() const
{
	return m_path;
}

void Gobby::Document::set_path(const Glib::ustring& new_path)
{
	m_path = new_path;
}

Glib::RefPtr<Gtk::SourceLanguage> Gobby::Document::get_language() const
{
	return get_buffer()->get_language();
}

void Gobby::Document::set_language(
	const Glib::RefPtr<Gtk::SourceLanguage>& language
)
{
	get_buffer()->set_language(language);
	m_signal_language_changed.emit();
}

const Gobby::Preferences& Gobby::Document::get_preferences() const
{
	return m_preferences;
}

void Gobby::Document::set_preferences(const Preferences& preferences)
{
	m_preferences = preferences;
	apply_preferences();
}

Glib::ustring Gobby::Document::get_content()
{
	return get_buffer()->get_text();
}

void Gobby::Document::obby_user_join(obby::user& user)
{
	update_tag_colour(user);
}

void Gobby::Document::obby_user_part(obby::user& user)
{
}

void Gobby::Document::obby_user_colour(obby::user& user)
{
	update_tag_colour(user);
}

void Gobby::Document::on_obby_insert(const obby::insert_record& record)
{
	if(m_editing) return;
	m_editing = true;

	// Get textbuffer
	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();

	// Translate position to row/column
	unsigned int row, col;
	m_doc.get_document()->position_to_coord(
		record.get_position(), row, col);

	// Find obby::user that inserted the text
	const obby::user* user = record.get_user();

	// Insert text
	Gtk::TextBuffer::iterator end = buffer->insert(
		buffer->get_iter_at_line_index(row, col),
		record.get_text()
	);

	// Colourize new text with that user's color
	Gtk::TextBuffer::iterator begin = end;
	begin.backward_chars(record.get_text().length() );
	update_user_colour(begin, end, user);

	m_editing = false;
}

void Gobby::Document::on_obby_delete(const obby::delete_record& record)
{
	if(m_editing) return;
	m_editing = true;

	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();
	unsigned int brow, bcol, erow, ecol;
	obby::local_document* doc = m_doc.get_document();

	doc->position_to_coord(record.get_begin(), brow, bcol);
	doc->position_to_coord(record.get_end(), erow, ecol);

	buffer->erase(
		buffer->get_iter_at_line_index(brow, bcol),
		buffer->get_iter_at_line_index(erow, ecol)
	);
	m_editing = false;
}

void Gobby::Document::on_obby_change_before()
{
}

void Gobby::Document::on_obby_change_after()
{
	// Document changed
	m_signal_content_changed.emit();
	// Cursor may have moved
	m_signal_cursor_moved.emit();
}

void Gobby::Document::on_obby_user_subscribe(const obby::user& user)
{
	// Call self function if the local user subscribed
	if(&user == &m_doc.get_buffer().get_self() )
		on_obby_self_subscribe();
}

void Gobby::Document::on_obby_user_unsubscribe(const obby::user& user)
{
	// Call self function if the local user unsubscribed
	if(&user == &m_doc.get_buffer().get_self() )
		on_obby_self_unsubscribe();
}

void Gobby::Document::on_obby_self_subscribe()
{
	// We are subscribed
	m_subscribed = true;

	// Get document we subscribed to
	obby::local_document& doc = *m_doc.get_document();
	Glib::RefPtr<Gtk::SourceBuffer> buf = get_buffer();

	// Install signal handlers
	doc.insert_event().before().connect(
		sigc::mem_fun(*this, &Document::on_obby_insert) );
	doc.delete_event().before().connect(
		sigc::mem_fun(*this, &Document::on_obby_delete) );
	doc.change_event().before().connect(
		sigc::mem_fun(*this, &Document::on_obby_change_before) );
	doc.change_event().after().connect(
		sigc::mem_fun(*this, &Document::on_obby_change_after) );

	// Set initial text
	m_editing = true;
	buf->set_text(doc.get_text() );

	// Make the document editable
	set_editable(true);

	// Apply preferences
	apply_preferences();

	// Enable highlighting
	buf->set_highlight(true);

	// Not modified when subscribed
	buf->set_modified(false);

	// Set initial authors
	for(unsigned int i = 0; i < doc.get_line_count(); ++ i)
	{
		// Get current line
		const obby::line& line = doc.get_line(i);
		obby::line::author_iterator prev = line.author_begin();
		obby::line::author_iterator cur = prev;

		// Iterate through it's authors list
		for(++ cur; prev != line.author_end(); ++ cur)
		{
			// Get current user
			const obby::user* user = prev->author;

			// user can be NULL (server insert event, but then
			// we do not have to apply a tag or so).
			if(user == NULL) { prev = cur; continue; }

			// Get the range to highlight
			obby::line::size_type endpos;
			if(cur != line.author_end() )
				endpos = cur->position;
			else
				endpos = line.length();

			Gtk::TextBuffer::iterator begin =
				buf->get_iter_at_line_index(i, prev->position);
			Gtk::TextBuffer::iterator end =
				buf->get_iter_at_line_index(i, endpos);

			// Apply corresponding tag
			buf->apply_tag_by_name(
				"gobby_user_" + user->get_name(),
				begin,
				end
			);

			prev = cur;
		}
	}

	m_editing = false;
}

void Gobby::Document::on_obby_self_unsubscribe()
{
	// We are not subscribed anymore
	m_subscribed = false;
	// Prevet from execution of signal handlers
	m_editing = true;
	// Apply preferences (which may change for non-subscribed documents)
	apply_preferences();
	// Set introduction text
	set_intro_text();
	// Re-enable signal handlers
	m_editing = false;
}

void Gobby::Document::on_gui_subscribe()
{
	// Send subscribe request
	m_doc.subscribe();
	// Deactivate the subscribe button since the request has been sent
	m_btn_subscribe.set_sensitive(false);
}

#if 0
// Hack to allow to drop files on a document. They will be opened as new
// documents if the contained data is an uri list, inserted into this document
// if its text.
bool Gobby::Document::on_drag_motion(
	const Glib::RefPtr<Gdk::DragContext>& context,
	int x, int y, guint32 time
)
{
	// Check available targets
	std::vector<std::string> targets = context->get_targets();
	for(unsigned int i = 0; i < targets.size(); ++ i)
		// Is one of them uri-lists?
		if(targets[i] == "text/uri-list")
			// Yes so stop here to not show the insertion marker
			// The event will be delayed to
			return false;

	// Call base function otherwise
	return Gtk::SourceView::on_drag_motion(context, x, y, time);
}
#endif

void Gobby::Document::on_insert_before(const Gtk::TextBuffer::iterator& begin,
                                       const Glib::ustring& text,
                                       int bytes)
{
	if(m_editing) return;
	m_editing = true;

	obby::local_document* doc = m_doc.get_document();
	doc->insert(
		doc->coord_to_position(
			begin.get_line(),
			begin.get_line_index()
		),
		text
	);

	m_editing = false;
}

void Gobby::Document::on_erase_before(const Gtk::TextBuffer::iterator& begin,
                                      const Gtk::TextBuffer::iterator& end)
{
	if(m_editing) return;
	m_editing = true;

	obby::local_document* doc = m_doc.get_document();
	doc->erase(
		doc->coord_to_position(
			begin.get_line(),
			begin.get_line_index()
		),
		doc->coord_to_position(
			end.get_line(),
			end.get_line_index()
		)
	);

	m_editing = false;
}

void Gobby::Document::on_insert_after(const Gtk::TextBuffer::iterator& end,
                                      const Glib::ustring& text,
                                      int bytes)
{
	// Other editing function is at work.
	if(!m_editing)
	{
		// Find the user that has written this text
		const obby::user& user = m_doc.get_buffer().get_self();

		// Find start position of new text
		Gtk::TextBuffer::iterator pos = end;
		pos.backward_chars(text.length() );

		// Update user colour. Set m_editing to true because this
		// colour update came from an editing operation. See
		// on_tag_apply below for more information on why this is
		// necessary.
		m_editing = true;
		update_user_colour(pos, end, &user);
		m_editing = false;

		// Cursor position has changed
		m_signal_cursor_moved.emit();
	}
}

void Gobby::Document::on_erase_after(const Gtk::TextBuffer::iterator& begin,
                                     const Gtk::TextBuffer::iterator& end)
{
	if(!m_editing)
	{
		// Cursor position may have changed
		m_signal_cursor_moved.emit();
	}
}

void Gobby::Document::on_apply_tag_after(const Glib::RefPtr<Gtk::TextTag>& tag,
                                         const Gtk::TextBuffer::iterator& begin,
                                         const Gtk::TextBuffer::iterator& end)
{
	Glib::ustring tag_name = tag->property_name();
	if(!m_editing && tag_name.compare(0, 10, "gobby_user") == 0)
	{
		// Not editing, but user tag is inserted. Not good. May result
		// from a copy+paste operation where tags where copied. Refresh
		// given range.
		unsigned int num_line = begin.get_line();
		unsigned int num_col = begin.get_line_index();

		// Find author of the text
		obby::local_document* doc = m_doc.get_document();
		const obby::line& line = doc->get_line(num_line);

		obby::line::author_iterator iter = line.author_begin();
		for(iter; iter != line.author_end(); ++ iter)
			if(iter->position > num_col)
				break;
		--iter;

		// Refresh.
		m_editing = true;
		update_user_colour(begin, end, iter->author);
		m_editing = false;
	}
}

void Gobby::Document::on_mark_set(
	const Gtk::TextBuffer::iterator& location,
	const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark
)
{
	// Mark was deleted or something
	if(!mark) return;

	// Insert mark changed position: Cursor position change
	if(mark == get_buffer()->get_insert() )
		m_signal_cursor_moved.emit();
}

void Gobby::Document::update_user_colour(const Gtk::TextBuffer::iterator& begin,
                                         const Gtk::TextBuffer::iterator& end,
                                         const obby::user* user)
{
	// Remove other user tags in that range
	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();
	Glib::RefPtr<Gtk::TextBuffer::TagTable> tag_table =
		buffer->get_tag_table();

	tag_table->foreach(
		sigc::bind(
			sigc::mem_fun(
				*this,
				&Document::on_remove_user_colour
			),
			sigc::ref(begin),
			sigc::ref(end)
		)
	);

	// If user is NULL (the server wrote this text), we have not to apply
	// any user tags.
	if(user == NULL) return;

	// Insert new user tag to the given range
	Glib::RefPtr<Gtk::TextTag> tag =
		tag_table->lookup("gobby_user_" + user->get_name() );

	// Apply tag twice to show it up immediately. I do not know why this
	// is necessary but if we do only apply it once, we have to wait for
	// the next event that refreshes the current line. I tried appending
	// queue_draw() after the apply_tag call, but it did not help
	// - Armin
	for(int i = 0; i < 2; ++ i)
		buffer->apply_tag(tag, begin, end);
}

void Gobby::Document::set_intro_text()
{
	Glib::RefPtr<Gtk::SourceBuffer> buf = get_buffer();

	// Build text
	obby::format_string str(_(
		"You are not subscribed to the document \"%0%\".\n\n"
		"To view changes that others make or to edit the document "
		"yourself, you have to subscribe to this document. Use the "
		"following button to perform this.\n\n"
	) );
	str << m_doc.get_title();

	// Set it
	buf->set_text(str.str() );

	// Add child anchor for the button
	Glib::RefPtr<Gtk::TextChildAnchor> anchor =
		buf->create_child_anchor(buf->end() );

	// Activate the subscribe button, if it isn't
	m_btn_subscribe.set_sensitive(true);

	// Add the button to the anchor
	add_child_at_anchor(m_btn_subscribe, anchor);

	// TODO: Add people that are currently subscribed
	set_editable(false);

	// Intro text is not modified
	get_buffer()->set_modified(false);

	// Do not highlight anything until the user subscribed
	buf->set_highlight(false);
}

void Gobby::Document::apply_preferences()
{
	// Editor
	set_tabs_width(m_preferences.editor.tab_width);
	set_insert_spaces_instead_of_tabs(m_preferences.editor.tab_spaces);
	set_auto_indent(m_preferences.editor.indentation_auto);
	set_smart_home_end(m_preferences.editor.homeend_smart);

	// View
	if(m_subscribed)
	{
		// Check preference for wrapped text
		if(m_preferences.view.wrap_text)
		{
			if(m_preferences.view.wrap_words)
				set_wrap_mode(Gtk::WRAP_CHAR);
			else
				set_wrap_mode(Gtk::WRAP_WORD);
		}
		else
		{
			set_wrap_mode(Gtk::WRAP_NONE);
		}
	}
	else
	{
		// Wrap when not subscribed (intro text)
		set_wrap_mode(Gtk::WRAP_WORD_CHAR);
	}

	set_show_line_numbers(m_preferences.view.linenum_display);
	set_highlight_current_line(m_preferences.view.curline_highlight);
	set_show_margin(m_preferences.view.margin_display);
	set_margin(m_preferences.view.margin_pos);
	get_buffer()->set_check_brackets(m_preferences.view.bracket_highlight);
}

void
Gobby::Document::on_remove_user_colour(Glib::RefPtr<Gtk::TextBuffer::Tag> tag,
                                       const Gtk::TextBuffer::iterator& begin,
				       const Gtk::TextBuffer::iterator& end)
{
	// Remove tag if it is a user color tag.
	Glib::ustring tag_name = tag->property_name();
	if(tag_name.compare(0, 10, "gobby_user") == 0)
		get_buffer()->remove_tag(tag, begin, end);
}

void Gobby::Document::update_tag_colour(obby::user& user)
{
	// Build tag name for this user
	Glib::ustring tag_name = "gobby_user_" + user.get_name();

	// Find already existing tag
	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();
	Glib::RefPtr<Gtk::TextBuffer::TagTable> tag_table =
		buffer->get_tag_table();
	Glib::RefPtr<Gtk::TextBuffer::Tag> tag = tag_table->lookup(tag_name);

	// Create new tag, if it doesn't exist
	if(!tag)
		tag = buffer->create_tag(tag_name);

	// Build color
	Gdk::Color color;
	color.set_red(user.get_red() * 65535 / 255);
	color.set_green(user.get_green() * 65535 / 255);
	color.set_blue(user.get_blue() * 65535 / 255);

	// Set/Update color
	tag->property_background_gdk() = color;
}

