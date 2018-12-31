/*************************************************************************/
/*  editor_profiler.cpp                                                  */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2018 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2018 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "editor_profiler.h"

#include "core/os/os.h"
#include "editor_scale.h"
#include "editor_settings.h"

// Export
#include "core/io/json.h"
#include "core/os/file_access.h"


// EditorProfiler::Metric

Dictionary EditorProfiler::Metric::to_dictionary() const {
	Dictionary res;

	res["valid"] = valid;
	res["frame_number"] = frame_number;
	res["frame_time"] = frame_time;
	res["idle_time"] = idle_time;
	res["physics_time"] = physics_time;
	res["physics_frame_time"] = physics_frame_time;
	Array category_arr;
	category_arr.resize(categories.size());
	for (int i = 0; i < categories.size(); i++) {

		const Category &c = categories[i];

		Dictionary category;
		category["signature"] = c.signature;
		category["name"] = c.name;
		category["total_time"] = c.total_time;

		Array item_arr;
		item_arr.resize(c.items.size());

		for (int j = 0; j < c.items.size(); j++) {
			Dictionary item;
			item["signature"] = c.items[j].signature;
			item["name"] = c.items[j].name;
			item["script"] = c.items[j].script;
			item["line"] = c.items[j].line;
			item["self"] = c.items[j].self;
			item["total"] = c.items[j].total;
			item["calls"] = c.items[j].calls;
			item_arr[j] = item;
		}
		category["items"] = item_arr;

		category_arr[i] = category;
	}
	res["categories"] = category_arr;

	return res;
}

void EditorProfiler::Metric::from_dictionary(const Dictionary &dict) {
	valid = dict.get("valid", false);
	if (!valid) {
		return;
	}
	frame_number = dict.get("frame_number", 0);
	frame_time = dict.get("frame_time", 0);
	idle_time = dict.get("idle_time", 0);
	physics_time = dict.get("physics_time", 0);
	physics_frame_time = dict.get("physics_frame_time", 0);

	const Array category_arr = dict.get("categories", Array());

	for (int i = 0; i < category_arr.size(); i++) {

		categories.resize(category_arr.size());
		Dictionary cat_d = category_arr[i];
		Category category;

		category.signature = cat_d.get("signature", "");
		category.name = cat_d.get("name", "");
		category.total_time = cat_d.get("total_time", 0);

		const Array item_arr = cat_d.get("items", Array());

		for (int j = 0; j < item_arr.size(); j++) {

			category.items.resize(item_arr.size());
			Category::Item item;
			Dictionary item_d = item_arr[j];
			item.signature = item_d.get("signature", "");
			item.name = item_d.get("name", "");
			item.script = item_d.get("script", "");
			item.line = item_d.get("line", 0);
			item.self = item_d.get("self", 0);
			item.total = item_d.get("total", 0);
			item.calls = item_d.get("calls", 0);
			category.items.write[j] = item;
		}
		categories.write[i] = category;
	}
}

// EditorProfiler

void EditorProfiler::_make_metric_ptrs(Metric &m) {

	for (int i = 0; i < m.categories.size(); i++) {
		m.category_ptrs[m.categories[i].signature] = &m.categories.write[i];
		for (int j = 0; j < m.categories[i].items.size(); j++) {
			m.item_ptrs[m.categories[i].items[j].signature] = &m.categories.write[i].items.write[j];
		}
	}
}

void EditorProfiler::add_frame_metric(const Metric &p_metric, bool p_final) {

	++last_metric;
	if (last_metric >= frame_metrics.size())
		last_metric = 0;

	frame_metrics.write[last_metric] = p_metric;
	_make_metric_ptrs(frame_metrics.write[last_metric]);

	updating_frame = true;
	cursor_metric_edit->set_max(frame_metrics[last_metric].frame_number);
	cursor_metric_edit->set_min(MAX(frame_metrics[last_metric].frame_number - frame_metrics.size(), 0));

	if (!seeking) {
		cursor_metric_edit->set_value(frame_metrics[last_metric].frame_number);
		if (hover_metric.x != -1) {
			hover_metric.x++;
			if (hover_metric.x >= frame_metrics.size()) {
				hover_metric.x = 0;
			}
		}
	}
	updating_frame = false;

	if (frame_delay->is_stopped()) {

		frame_delay->set_wait_time(p_final ? 0.1 : 1);
		frame_delay->start();
	}

	if (plot_delay->is_stopped()) {
		plot_delay->set_wait_time(0.1);
		plot_delay->start();
	}
}

void EditorProfiler::clear() {

	int metric_size = EditorSettings::get_singleton()->get("debugger/profiler_frame_history_size");
	metric_size = CLAMP(metric_size, 60, 1024);
	frame_metrics.clear();
	frame_metrics.resize(metric_size);
	last_metric = -1;
	variables->clear();
	//activate->set_pressed(false);
	plot_sigs.clear();
	plot_sigs.insert("physics_frame_time");
	plot_sigs.insert("category_frame_time");

	updating_frame = true;
	cursor_metric_edit->set_min(0);
	cursor_metric_edit->set_max(0);
	cursor_metric_edit->set_value(0);
	updating_frame = false;
	hover_metric.x = -1;
	seeking = false;
}

static String _get_percent_txt(float p_value, float p_total) {
	if (p_total == 0)
		p_total = 0.00001;
	return String::num((p_value / p_total) * 100, 1) + "%";
}

String EditorProfiler::_get_time_as_text(const Metric &m, float p_time, int p_calls) {

	int dmode = display_mode->get_selected();

	if (dmode == DISPLAY_FRAME_TIME) {
		return rtos(p_time);
	} else if (dmode == DISPLAY_AVERAGE_TIME) {
		if (p_calls == 0)
			return "0";
		else
			return rtos(p_time / p_calls);
	} else if (dmode == DISPLAY_FRAME_PERCENT) {
		return _get_percent_txt(p_time, m.frame_time);
	} else if (dmode == DISPLAY_PHYSICS_FRAME_PERCENT) {

		return _get_percent_txt(p_time, m.physics_frame_time);
	}

	return "err";
}

Color EditorProfiler::_get_color_from_signature(const StringName &p_signature) const {

	Color bc = get_color("error_color", "Editor");
	double rot = ABS(double(p_signature.hash()) / double(0x7FFFFFFF));
	Color c;
	c.set_hsv(rot, bc.get_s(), bc.get_v());
	return c.linear_interpolate(get_color("base_color", "Editor"), 0.07);
}

void EditorProfiler::_item_edited() {

	if (updating_frame)
		return;

	TreeItem *item = variables->get_edited();
	if (!item)
		return;
	StringName signature = item->get_metadata(0);
	bool checked = item->is_checked(0);

	if (checked)
		plot_sigs.insert(signature);
	else
		plot_sigs.erase(signature);

	if (!frame_delay->is_processing()) {
		frame_delay->set_wait_time(0.1);
		frame_delay->start();
	}

	_update_plot();
}

void EditorProfiler::_update_plot() {

	int w = graph->get_size().width;
	int h = graph->get_size().height;

	bool reset_texture = false;

	int desired_len = w * h * 4;

	if (graph_image.size() != desired_len) {
		reset_texture = true;
		graph_image.resize(desired_len);
	}

	PoolVector<uint8_t>::Write wr = graph_image.write();

	//clear
	for (int i = 0; i < desired_len; i += 4) {
		wr[i + 0] = 0;
		wr[i + 1] = 0;
		wr[i + 2] = 0;
		wr[i + 3] = 255;
	}

	//find highest value

	bool use_self = display_time->get_selected() == DISPLAY_SELF_TIME;
	float highest = 0;

	for (int i = 0; i < frame_metrics.size(); i++) {
		const Metric &m = frame_metrics[i];
		if (!m.valid)
			continue;

		for (Set<StringName>::Element *E = plot_sigs.front(); E; E = E->next()) {

			const Map<StringName, Metric::Category *>::Element *F = m.category_ptrs.find(E->get());
			if (F) {
				highest = MAX(F->get()->total_time, highest);
			}

			const Map<StringName, Metric::Category::Item *>::Element *G = m.item_ptrs.find(E->get());
			if (G) {
				if (use_self) {
					highest = MAX(G->get()->self, highest);
				} else {
					highest = MAX(G->get()->total, highest);
				}
			}
		}
	}

	if (highest > 0) {
		//means some data exists..
		highest *= 1.2; //leave some upper room
		graph_height = highest;

		Vector<int> columnv;
		columnv.resize(h * 4);

		int *column = columnv.ptrw();

		Map<StringName, int> plot_prev;
		//Map<StringName,int> plot_max;

		uint64_t time = OS::get_singleton()->get_ticks_usec();

		for (int i = 0; i < w; i++) {

			for (int j = 0; j < h * 4; j++) {
				column[j] = 0;
			}

			int current = i * frame_metrics.size() / w;
			int next = (i + 1) * frame_metrics.size() / w;
			if (next > frame_metrics.size()) {
				next = frame_metrics.size();
			}
			if (next == current)
				next = current + 1; //just because for loop must work

			for (Set<StringName>::Element *E = plot_sigs.front(); E; E = E->next()) {

				int plot_pos = -1;

				for (int j = current; j < next; j++) {

					//wrap
					int idx = last_metric + 1 + j;
					while (idx >= frame_metrics.size()) {
						idx -= frame_metrics.size();
					}

					//get
					const Metric &m = frame_metrics[idx];
					if (!m.valid)
						continue; //skip because invalid

					float value = 0;

					const Map<StringName, Metric::Category *>::Element *F = m.category_ptrs.find(E->get());
					if (F) {
						value = F->get()->total_time;
					}

					const Map<StringName, Metric::Category::Item *>::Element *G = m.item_ptrs.find(E->get());
					if (G) {
						if (use_self) {
							value = G->get()->self;
						} else {
							value = G->get()->total;
						}
					}

					plot_pos = MAX(CLAMP(int(value * h / highest), 0, h - 1), plot_pos);
				}

				int prev_plot = plot_pos;
				Map<StringName, int>::Element *H = plot_prev.find(E->get());
				if (H) {
					prev_plot = H->get();
					H->get() = plot_pos;
				} else {
					plot_prev[E->get()] = plot_pos;
				}

				if (plot_pos == -1 && prev_plot == -1) {
					//don't bother drawing
					continue;
				}

				if (prev_plot != -1 && plot_pos == -1) {

					plot_pos = prev_plot;
				}

				if (prev_plot == -1 && plot_pos != -1) {
					prev_plot = plot_pos;
				}

				plot_pos = h - plot_pos - 1;
				prev_plot = h - prev_plot - 1;

				if (prev_plot > plot_pos) {
					SWAP(prev_plot, plot_pos);
				}

				Color col = _get_color_from_signature(E->get());

				for (int j = prev_plot; j <= plot_pos; j++) {

					column[j * 4 + 0] += Math::fast_ftoi(CLAMP(col.r * 255, 0, 255));
					column[j * 4 + 1] += Math::fast_ftoi(CLAMP(col.g * 255, 0, 255));
					column[j * 4 + 2] += Math::fast_ftoi(CLAMP(col.b * 255, 0, 255));
					column[j * 4 + 3] += 1;
				}
			}

			for (int j = 0; j < h * 4; j += 4) {

				int a = column[j + 3];
				if (a > 0) {
					column[j + 0] /= a;
					column[j + 1] /= a;
					column[j + 2] /= a;
				}

				uint8_t r = uint8_t(column[j + 0]);
				uint8_t g = uint8_t(column[j + 1]);
				uint8_t b = uint8_t(column[j + 2]);

				int widx = ((j >> 2) * w + i) * 4;
				wr[widx + 0] = r;
				wr[widx + 1] = g;
				wr[widx + 2] = b;
				wr[widx + 3] = 255;
			}
		}

		time = OS::get_singleton()->get_ticks_usec() - time;
	}

	wr = PoolVector<uint8_t>::Write();

	Ref<Image> img;
	img.instance();
	img->create(w, h, 0, Image::FORMAT_RGBA8, graph_image);

	if (reset_texture) {

		if (graph_texture.is_null()) {
			graph_texture.instance();
		}
		graph_texture->create(img->get_width(), img->get_height(), img->get_format(), Texture::FLAG_VIDEO_SURFACE);
	}

	graph_texture->set_data(img);

	graph->set_texture(graph_texture);
	graph->update();
}

void EditorProfiler::_update_frame() {

	int cursor_metric = _get_cursor_index();

	ERR_FAIL_INDEX(cursor_metric, frame_metrics.size());

	updating_frame = true;
	variables->clear();

	TreeItem *root = variables->create_item();
	const Metric &m = frame_metrics[cursor_metric];

	int dtime = display_time->get_selected();

	for (int i = 0; i < m.categories.size(); i++) {

		TreeItem *category = variables->create_item(root);
		category->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
		category->set_editable(0, true);
		category->set_metadata(0, m.categories[i].signature);
		category->set_text(0, String(m.categories[i].name));
		category->set_text(1, _get_time_as_text(m, m.categories[i].total_time, 1));

		if (plot_sigs.has(m.categories[i].signature)) {
			category->set_checked(0, true);
			category->set_custom_color(0, _get_color_from_signature(m.categories[i].signature));
		}

		for (int j = 0; j < m.categories[i].items.size(); j++) {
			const Metric::Category::Item &it = m.categories[i].items[j];

			TreeItem *item = variables->create_item(category);
			item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
			item->set_editable(0, true);
			item->set_text(0, it.name);
			item->set_metadata(0, it.signature);
			item->set_metadata(1, it.script);
			item->set_metadata(2, it.line);
			item->set_tooltip(0, it.script + ":" + itos(it.line));

			float time = dtime == DISPLAY_SELF_TIME ? it.self : it.total;

			item->set_text(1, _get_time_as_text(m, time, it.calls));

			item->set_text(2, itos(it.calls));

			if (plot_sigs.has(it.signature)) {
				item->set_checked(0, true);
				item->set_custom_color(0, _get_color_from_signature(it.signature));
			}
		}
	}

	updating_frame = false;
}

void EditorProfiler::_activate_pressed() {

	if (activate->is_pressed()) {
		activate->set_icon(get_icon("Stop", "EditorIcons"));
		activate->set_text(TTR("Stop"));
	} else {
		activate->set_icon(get_icon("Play", "EditorIcons"));
		activate->set_text(TTR("Start"));
	}
	emit_signal("enable_profiling", activate->is_pressed());
}

void EditorProfiler::_clear_pressed() {

	clear();
	_update_plot();
}

void EditorProfiler::_import_pressed() {

	file_dialog->set_mode(EditorFileDialog::MODE_OPEN_FILE);
	file_dialog->set_title(TTR("Open Profiling Data"));
	file_dialog->popup_centered_ratio();
}

void EditorProfiler::_export_pressed() {

	file_dialog->set_mode(EditorFileDialog::MODE_SAVE_FILE);
	file_dialog->set_title(TTR("Save Profiling Data As..."));
	file_dialog->popup_centered_ratio();

}

void EditorProfiler::_notification(int p_what) {

	if (p_what == NOTIFICATION_ENTER_TREE) {
		activate->set_icon(get_icon("Play", "EditorIcons"));
		clear_button->set_icon(get_icon("Clear", "EditorIcons"));
	}
}

void EditorProfiler::_graph_tex_draw() {

	if (last_metric < 0)
		return;
	if (seeking) {

		int max_frames = frame_metrics.size();
		int frame = cursor_metric_edit->get_value() - (frame_metrics[last_metric].frame_number - max_frames + 1);
		if (frame < 0)
			frame = 0;

		int cur_x = frame * graph->get_size().x / max_frames;

		graph->draw_line(Vector2(cur_x, 0), Vector2(cur_x, graph->get_size().y), Color(1, 1, 1, 0.8));
	}

	const Metric &metric = frame_metrics[hover_metric.x];
	if (hover_metric.x != -1 && metric.valid) {

		int max_frames = frame_metrics.size();
		int frame = metric.frame_number - (frame_metrics[last_metric].frame_number - max_frames + 1);
		if (frame < 0)
			frame = 0;

		int cur_x = frame * graph->get_size().x / max_frames;

		graph->draw_line(Vector2(cur_x, 0), Vector2(cur_x, graph->get_size().y), Color(1, 1, 1, 0.4));
		
		// Frame time preview
		StringName closest_signature;
		float value = -1;
		// convert metric value to graph height
		float conv_factor = graph->get_size().height / graph_height;

		for (int i = 0; i < metric.categories.size(); i++) {

			if (plot_sigs.has(metric.categories[i].signature)) {
				metric.categories[i].total_time;
			}

			for (int j = 0; j < metric.categories[i].items.size(); j++) {
				const Metric::Category::Item &it = metric.categories[i].items[j];

				if (plot_sigs.has(it.signature)) {
					it.total;
				}
			}
		}

		if (value != -1) {
			
			// _get_color_from_signature(it.signature)
			Ref<Font> frame_time_font = this->get_font("font", "Label");
			graph->draw_string(frame_time_font,
				Vector2(cur_x + 2, frame_time_font->get_height()),
				String::num_real(value));
		}
	}
}

void EditorProfiler::_graph_tex_mouse_exit() {

	hover_metric.x = -1;
	graph->update();
}

void EditorProfiler::_cursor_metric_changed(double) {
	if (updating_frame)
		return;

	graph->update();
	_update_frame();
}

void EditorProfiler::_graph_tex_input(const Ref<InputEvent> &p_ev) {

	if (last_metric < 0)
		return;

	Ref<InputEventMouse> me = p_ev;
	Ref<InputEventMouseButton> mb = p_ev;
	Ref<InputEventMouseMotion> mm = p_ev;

	if (
			(mb.is_valid() && mb->get_button_index() == BUTTON_LEFT && mb->is_pressed()) ||
			(mm.is_valid())) {

		int x = me->get_position().x;
		x = x * frame_metrics.size() / graph->get_size().width;

		bool show_hover = x >= 0 && x < frame_metrics.size();

		if (x < 0) {
			x = 0;
		}

		if (x >= frame_metrics.size()) {
			x = frame_metrics.size() - 1;
		}

		int metric = frame_metrics.size() - x - 1;
		metric = last_metric - metric;
		while (metric < 0) {
			metric += frame_metrics.size();
		}

		if (show_hover) {

			hover_metric = Vector2i(metric, me->get_position().y); // TODO

		} else {
			hover_metric.x = -1;
		}

		if (mb.is_valid() || mm->get_button_mask() & BUTTON_MASK_LEFT) {
			//cursor_metric=x;
			updating_frame = true;

			//metric may be invalid, so look for closest metric that is valid, this makes snap feel better
			bool valid = false;
			for (int i = 0; i < frame_metrics.size(); i++) {

				if (frame_metrics[metric].valid) {
					valid = true;
					break;
				}

				metric++;
				if (metric >= frame_metrics.size())
					metric = 0;
			}

			if (valid)
				cursor_metric_edit->set_value(frame_metrics[metric].frame_number);

			updating_frame = false;

			if (activate->is_pressed()) {
				if (!seeking) {
					emit_signal("break_request");
				}
			}

			seeking = true;

			if (!frame_delay->is_processing()) {
				frame_delay->set_wait_time(0.1);
				frame_delay->start();
			}
		}

		graph->update();
	}
}

int EditorProfiler::_get_cursor_index() const {

	if (last_metric < 0)
		return 0;
	if (!frame_metrics[last_metric].valid)
		return 0;

	int diff = (frame_metrics[last_metric].frame_number - cursor_metric_edit->get_value());

	int idx = last_metric - diff;
	while (idx < 0) {
		idx += frame_metrics.size();
	}

	return idx;
}

void EditorProfiler::disable_seeking() {

	seeking = false;
	graph->update();
}

void EditorProfiler::_combo_changed(int) {

	_update_frame();
	_update_plot();
}

void EditorProfiler::_file_dialog_callback(const String &p_string) {

	if (file_dialog->get_mode() == EditorFileDialog::MODE_OPEN_FILE) { // Import

		Error file_err;
		FileAccess *file = FileAccess::open(p_string, FileAccess::READ, &file_err);

		if (file_err) {
			if (file)
				memdelete(file);
			return;
		}

		String imported_json = file->get_line();

		memdelete(file);

		Variant json_data;
		String err_str;
		int err_line;
		Error json_err = JSON::parse(imported_json, json_data, err_str, err_line);

		if (json_err != OK) {
			_err_print_error("", p_string.utf8().get_data(), err_line, err_str.utf8().get_data(), ERR_HANDLER_SCRIPT);
			return;
		}
		
		updating_frame = true;

		Dictionary data_dict = json_data;
		
		last_metric = data_dict.get("last_metric", -1);

		// clear
		frame_metrics.clear();
		variables->clear();
		plot_sigs.clear();
		hover_metric.x = -1;
		seeking = false;
		
		// Import frame_metrics
		Array metrics_arr = data_dict.get("frame_metrics", Array());
		if (metrics_arr.size() == 0) {
			return;
		}

		frame_metrics.resize(metrics_arr.size());

		for (int i = 0; i < metrics_arr.size(); i++) {
			Metric metric;
			Dictionary dict_metric = metrics_arr[i];
			metric.from_dictionary(dict_metric);
			_make_metric_ptrs(metric);
			frame_metrics.write[i] = metric;
		}

		// Import plot_sigs
		Array sigs_arr = data_dict.get("plot_sigs", Array());

		for (int i = 0; i < sigs_arr.size(); i++) {
			StringName sn = sigs_arr[i];
			plot_sigs.insert(sn);
		}
		
		cursor_metric_edit->set_max(frame_metrics[last_metric].frame_number);
		cursor_metric_edit->set_min(MAX(frame_metrics[last_metric].frame_number - frame_metrics.size(), 0));
		cursor_metric_edit->set_value(frame_metrics[0].frame_number);
		
		updating_frame = false;

		_update_frame();
		_update_plot();

	} else if (file_dialog->get_mode() == EditorFileDialog::MODE_SAVE_FILE) { // Export

		Dictionary dict;

		dict["last_metric"] = last_metric;
		
		// Export frame_metrics
		Array metrics_arr;
		metrics_arr.resize(frame_metrics.size());
		for (int i = 0; i < frame_metrics.size(); i++) {
			metrics_arr[i] = frame_metrics[i].to_dictionary();
		}
		dict["frame_metrics"] = metrics_arr;

		// Export plot_sigs
		Array sigs_arr;
		sigs_arr.resize(plot_sigs.size());
		int sigs_index = 0;
		for (Set<StringName>::Element *E = plot_sigs.front(); E; E = E->next()) {
			sigs_arr[sigs_index++] = E->get();
		}
		dict["plot_sigs"] = sigs_arr;
		
		const String json_data = JSON::print(dict);

		Error err;
		FileAccess *file = FileAccess::open(p_string, FileAccess::WRITE, &err);

		if (err) {
			if (file)
				memdelete(file);
			return;
		}

		file->store_string(json_data);

		memdelete(file);
	}
}

void EditorProfiler::_bind_methods() {

	ClassDB::bind_method(D_METHOD("_update_frame"), &EditorProfiler::_update_frame);
	ClassDB::bind_method(D_METHOD("_update_plot"), &EditorProfiler::_update_plot);
	ClassDB::bind_method(D_METHOD("_activate_pressed"), &EditorProfiler::_activate_pressed);
	ClassDB::bind_method(D_METHOD("_clear_pressed"), &EditorProfiler::_clear_pressed);
	ClassDB::bind_method(D_METHOD("_import_pressed"), &EditorProfiler::_import_pressed);
	ClassDB::bind_method(D_METHOD("_export_pressed"), &EditorProfiler::_export_pressed);
	ClassDB::bind_method(D_METHOD("_graph_tex_draw"), &EditorProfiler::_graph_tex_draw);
	ClassDB::bind_method(D_METHOD("_graph_tex_input"), &EditorProfiler::_graph_tex_input);
	ClassDB::bind_method(D_METHOD("_graph_tex_mouse_exit"), &EditorProfiler::_graph_tex_mouse_exit);
	ClassDB::bind_method(D_METHOD("_cursor_metric_changed"), &EditorProfiler::_cursor_metric_changed);
	ClassDB::bind_method(D_METHOD("_combo_changed"), &EditorProfiler::_combo_changed);

	ClassDB::bind_method("_file_dialog_callback", &EditorProfiler::_file_dialog_callback);

	ClassDB::bind_method(D_METHOD("_item_edited"), &EditorProfiler::_item_edited);
	ADD_SIGNAL(MethodInfo("enable_profiling", PropertyInfo(Variant::BOOL, "enable")));
	ADD_SIGNAL(MethodInfo("break_request"));
}

void EditorProfiler::set_enabled(bool p_enable) {

	activate->set_disabled(!p_enable);
}

bool EditorProfiler::is_profiling() {
	return activate->is_pressed();
}

EditorProfiler::EditorProfiler() {

	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog->connect("file_selected", this, "_file_dialog_callback");
	add_child(file_dialog);

	HBoxContainer *hb = memnew(HBoxContainer);
	add_child(hb);
	activate = memnew(Button);
	activate->set_toggle_mode(true);
	activate->set_text(TTR("Start"));
	activate->connect("pressed", this, "_activate_pressed");
	hb->add_child(activate);

	clear_button = memnew(Button);
	clear_button->set_text(TTR("Clear"));
	clear_button->connect("pressed", this, "_clear_pressed");
	hb->add_child(clear_button);

	import_button = memnew(Button);
	import_button->set_text(TTR("Import"));
	import_button->connect("pressed", this, "_import_pressed");
	hb->add_child(import_button);

	export_button = memnew(Button);
	export_button->set_text(TTR("Export"));
	export_button->connect("pressed", this, "_export_pressed");
	hb->add_child(export_button);

	hb->add_child(memnew(Label(TTR("Measure:"))));

	display_mode = memnew(OptionButton);
	display_mode->add_item(TTR("Frame Time (sec)"));
	display_mode->add_item(TTR("Average Time (sec)"));
	display_mode->add_item(TTR("Frame %"));
	display_mode->add_item(TTR("Physics Frame %"));
	display_mode->connect("item_selected", this, "_combo_changed");

	hb->add_child(display_mode);

	hb->add_child(memnew(Label(TTR("Time:"))));

	display_time = memnew(OptionButton);
	display_time->add_item(TTR("Inclusive"));
	display_time->add_item(TTR("Self"));
	display_time->connect("item_selected", this, "_combo_changed");

	hb->add_child(display_time);

	hb->add_spacer();

	hb->add_child(memnew(Label(TTR("Frame #:"))));

	cursor_metric_edit = memnew(SpinBox);
	cursor_metric_edit->set_h_size_flags(SIZE_FILL);
	hb->add_child(cursor_metric_edit);
	cursor_metric_edit->connect("value_changed", this, "_cursor_metric_changed");

	hb->add_constant_override("separation", 8 * EDSCALE);

	h_split = memnew(HSplitContainer);
	add_child(h_split);
	h_split->set_v_size_flags(SIZE_EXPAND_FILL);

	variables = memnew(Tree);
	variables->set_custom_minimum_size(Size2(300, 0) * EDSCALE);
	variables->set_hide_folding(true);
	h_split->add_child(variables);
	variables->set_hide_root(true);
	variables->set_columns(3);
	variables->set_column_titles_visible(true);
	variables->set_column_title(0, TTR("Name"));
	variables->set_column_expand(0, true);
	variables->set_column_min_width(0, 60);
	variables->set_column_title(1, TTR("Time"));
	variables->set_column_expand(1, false);
	variables->set_column_min_width(1, 60 * EDSCALE);
	variables->set_column_title(2, TTR("Calls"));
	variables->set_column_expand(2, false);
	variables->set_column_min_width(2, 60 * EDSCALE);
	variables->connect("item_edited", this, "_item_edited");

	graph = memnew(TextureRect);
	graph->set_expand(true);
	graph->set_mouse_filter(MOUSE_FILTER_STOP);
	//graph->set_ignore_mouse(false);
	graph->connect("draw", this, "_graph_tex_draw");
	graph->connect("gui_input", this, "_graph_tex_input");
	graph->connect("mouse_exited", this, "_graph_tex_mouse_exit");

	h_split->add_child(graph);
	graph->set_h_size_flags(SIZE_EXPAND_FILL);

	int metric_size = CLAMP(int(EDITOR_DEF("debugger/profiler_frame_history_size", 600)), 60, 1024);
	frame_metrics.resize(metric_size);
	last_metric = -1;
	//cursor_metric=-1;
	hover_metric.x = -1;

	EDITOR_DEF("debugger/profiler_frame_max_functions", 64);

	//display_mode=DISPLAY_FRAME_TIME;

	frame_delay = memnew(Timer);
	frame_delay->set_wait_time(0.1);
	frame_delay->set_one_shot(true);
	add_child(frame_delay);
	frame_delay->connect("timeout", this, "_update_frame");

	plot_delay = memnew(Timer);
	plot_delay->set_wait_time(0.1);
	plot_delay->set_one_shot(true);
	add_child(plot_delay);
	plot_delay->connect("timeout", this, "_update_plot");

	plot_sigs.insert("physics_frame_time");
	plot_sigs.insert("category_frame_time");

	seeking = false;
	graph_height = 1;

	//activate->set_disabled(true);
}
