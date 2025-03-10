/*************************************************************************/
/*  tiles_editor_plugin.cpp                                              */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
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

#include "tiles_editor_plugin.h"

#include "core/os/mutex.h"

#include "editor/editor_node.h"
#include "editor/editor_scale.h"
#include "editor/plugins/canvas_item_editor_plugin.h"

#include "scene/2d/tile_map.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/separator.h"
#include "scene/resources/tile_set.h"

#include "tile_set_editor.h"

TilesEditorPlugin *TilesEditorPlugin::singleton = nullptr;

void TilesEditorPlugin::_preview_frame_started() {
	RS::get_singleton()->request_frame_drawn_callback(callable_mp(const_cast<TilesEditorPlugin *>(this), &TilesEditorPlugin::_pattern_preview_done));
}

void TilesEditorPlugin::_pattern_preview_done() {
	pattern_preview_done.post();
}

void TilesEditorPlugin::_thread_func(void *ud) {
	TilesEditorPlugin *te = static_cast<TilesEditorPlugin *>(ud);
	te->_thread();
}

void TilesEditorPlugin::_thread() {
	pattern_thread_exited.clear();
	while (!pattern_thread_exit.is_set()) {
		pattern_preview_sem.wait();

		pattern_preview_mutex.lock();
		if (pattern_preview_queue.size()) {
			QueueItem item = pattern_preview_queue.front()->get();
			pattern_preview_queue.pop_front();
			pattern_preview_mutex.unlock();

			int thumbnail_size = EditorSettings::get_singleton()->get("filesystem/file_dialog/thumbnail_size");
			thumbnail_size *= EDSCALE;
			Vector2 thumbnail_size2 = Vector2(thumbnail_size, thumbnail_size);

			if (item.pattern.is_valid() && !item.pattern->is_empty()) {
				// Generate the pattern preview
				SubViewport *viewport = memnew(SubViewport);
				viewport->set_size(thumbnail_size2);
				viewport->set_disable_input(true);
				viewport->set_transparent_background(true);
				viewport->set_update_mode(SubViewport::UPDATE_ONCE);

				TileMap *tile_map = memnew(TileMap);
				tile_map->set_tileset(item.tile_set);
				tile_map->set_pattern(0, Vector2(), item.pattern);
				viewport->add_child(tile_map);

				TypedArray<Vector2i> used_cells = tile_map->get_used_cells(0);

				Rect2 encompassing_rect = Rect2();
				encompassing_rect.set_position(tile_map->map_to_world(used_cells[0]));
				for (int i = 0; i < used_cells.size(); i++) {
					Vector2i cell = used_cells[i];
					Vector2 world_pos = tile_map->map_to_world(cell);
					encompassing_rect.expand_to(world_pos);

					// Texture.
					Ref<TileSetAtlasSource> atlas_source = tile_set->get_source(tile_map->get_cell_source_id(0, cell));
					if (atlas_source.is_valid()) {
						Vector2i coords = tile_map->get_cell_atlas_coords(0, cell);
						int alternative = tile_map->get_cell_alternative_tile(0, cell);

						Vector2 center = world_pos - atlas_source->get_tile_effective_texture_offset(coords, alternative);
						encompassing_rect.expand_to(center - atlas_source->get_tile_texture_region(coords).size / 2);
						encompassing_rect.expand_to(center + atlas_source->get_tile_texture_region(coords).size / 2);
					}
				}

				Vector2 scale = thumbnail_size2 / MAX(encompassing_rect.size.x, encompassing_rect.size.y);
				tile_map->set_scale(scale);
				tile_map->set_position(-(scale * encompassing_rect.get_center()) + thumbnail_size2 / 2);

				// Add the viewport at the last moment to avoid rendering too early.
				EditorNode::get_singleton()->add_child(viewport);

				RS::get_singleton()->connect(SNAME("frame_pre_draw"), callable_mp(const_cast<TilesEditorPlugin *>(this), &TilesEditorPlugin::_preview_frame_started), Vector<Variant>(), Object::CONNECT_ONESHOT);

				pattern_preview_done.wait();

				Ref<Image> image = viewport->get_texture()->get_image();

				// Find the index for the given pattern. TODO: optimize.
				Variant args[] = { item.pattern, ImageTexture::create_from_image(image) };
				const Variant *args_ptr[] = { &args[0], &args[1] };
				Variant r;
				Callable::CallError error;
				item.callback.call(args_ptr, 2, r, error);

				viewport->queue_delete();
			} else {
				pattern_preview_mutex.unlock();
			}
		}
	}
	pattern_thread_exited.set();
}

void TilesEditorPlugin::_tile_map_changed() {
	tile_map_changed_needs_update = true;
}

void TilesEditorPlugin::_update_editors() {
	// If tile_map is not edited, we change the edited only if we are not editing a tile_set.
	tileset_editor->edit(tile_set);
	TileMap *tile_map = Object::cast_to<TileMap>(ObjectDB::get_instance(tile_map_id));
	if (tile_map) {
		tilemap_editor->edit(tile_map);
	} else {
		tilemap_editor->edit(nullptr);
	}

	// Update the viewport.
	CanvasItemEditor::get_singleton()->update_viewport();

	// Update visibility of bottom panel buttons.
	if (tileset_editor_button->is_pressed() && !tile_set.is_valid()) {
		if (tile_map) {
			EditorNode::get_singleton()->make_bottom_panel_item_visible(tilemap_editor);
		} else {
			EditorNode::get_singleton()->hide_bottom_panel();
		}
	}
}

void TilesEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (tile_map_changed_needs_update) {
				TileMap *tile_map = Object::cast_to<TileMap>(ObjectDB::get_instance(tile_map_id));
				if (tile_map) {
					tile_set = tile_map->get_tileset();
				}
				_update_editors();
				tile_map_changed_needs_update = false;
			}
		} break;
	}
}

void TilesEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		// Disable and hide invalid editors.
		TileMap *tile_map = Object::cast_to<TileMap>(ObjectDB::get_instance(tile_map_id));
		tileset_editor_button->set_visible(tile_set.is_valid());
		tilemap_editor_button->set_visible(tile_map);
		if (tile_map) {
			EditorNode::get_singleton()->make_bottom_panel_item_visible(tilemap_editor);
		} else {
			EditorNode::get_singleton()->make_bottom_panel_item_visible(tileset_editor);
		}

	} else {
		tileset_editor_button->hide();
		tilemap_editor_button->hide();
		EditorNode::get_singleton()->hide_bottom_panel();
	}
}

void TilesEditorPlugin::queue_pattern_preview(Ref<TileSet> p_tile_set, Ref<TileMapPattern> p_pattern, Callable p_callback) {
	ERR_FAIL_COND(!p_tile_set.is_valid());
	ERR_FAIL_COND(!p_pattern.is_valid());
	{
		MutexLock lock(pattern_preview_mutex);
		pattern_preview_queue.push_back({ p_tile_set, p_pattern, p_callback });
	}
	pattern_preview_sem.post();
}

void TilesEditorPlugin::set_sources_lists_current(int p_current) {
	atlas_sources_lists_current = p_current;
}

void TilesEditorPlugin::synchronize_sources_list(Object *p_current_list, Object *p_current_sort_button) {
	ItemList *item_list = Object::cast_to<ItemList>(p_current_list);
	MenuButton *sorting_button = Object::cast_to<MenuButton>(p_current_sort_button);
	ERR_FAIL_COND(!item_list);
	ERR_FAIL_COND(!sorting_button);

	if (sorting_button->is_visible_in_tree()) {
		for (int i = 0; i != SOURCE_SORT_MAX; i++) {
			sorting_button->get_popup()->set_item_checked(i, (i == (int)source_sort));
		}
	}

	if (item_list->is_visible_in_tree()) {
		// Make sure the selection is not overwritten after sorting.
		int atlas_sources_lists_current_mem = atlas_sources_lists_current;
		item_list->emit_signal(SNAME("sort_request"));
		atlas_sources_lists_current = atlas_sources_lists_current_mem;

		if (atlas_sources_lists_current < 0 || atlas_sources_lists_current >= item_list->get_item_count()) {
			item_list->deselect_all();
		} else {
			item_list->set_current(atlas_sources_lists_current);
			item_list->ensure_current_is_visible();
			item_list->emit_signal(SNAME("item_selected"), atlas_sources_lists_current);
		}
	}
}

void TilesEditorPlugin::set_atlas_view_transform(float p_zoom, Vector2 p_scroll) {
	atlas_view_zoom = p_zoom;
	atlas_view_scroll = p_scroll;
}

void TilesEditorPlugin::synchronize_atlas_view(Object *p_current) {
	TileAtlasView *tile_atlas_view = Object::cast_to<TileAtlasView>(p_current);
	ERR_FAIL_COND(!tile_atlas_view);

	if (tile_atlas_view->is_visible_in_tree()) {
		tile_atlas_view->set_transform(atlas_view_zoom, atlas_view_scroll);
	}
}

void TilesEditorPlugin::set_sorting_option(int p_option) {
	source_sort = p_option;
}

List<int> TilesEditorPlugin::get_sorted_sources(const Ref<TileSet> tile_set) const {
	SourceNameComparator::tile_set = tile_set;
	List<int> source_ids;

	for (int i = 0; i < tile_set->get_source_count(); i++) {
		source_ids.push_back(tile_set->get_source_id(i));
	}

	switch (source_sort) {
		case SOURCE_SORT_ID_REVERSE:
			// Already sorted.
			source_ids.reverse();
			break;
		case SOURCE_SORT_NAME:
			source_ids.sort_custom<SourceNameComparator>();
			break;
		case SOURCE_SORT_NAME_REVERSE:
			source_ids.sort_custom<SourceNameComparator>();
			source_ids.reverse();
			break;
		default: // SOURCE_SORT_ID
			break;
	}

	SourceNameComparator::tile_set.unref();
	return source_ids;
}

Ref<TileSet> TilesEditorPlugin::SourceNameComparator::tile_set;

bool TilesEditorPlugin::SourceNameComparator::operator()(const int &p_a, const int &p_b) const {
	String name_a;
	String name_b;

	{
		TileSetSource *source = *tile_set->get_source(p_a);

		if (!source->get_name().is_empty()) {
			name_a = source->get_name();
		}

		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		if (atlas_source) {
			Ref<Texture2D> texture = atlas_source->get_texture();
			if (name_a.is_empty() && texture.is_valid()) {
				name_a = texture->get_path().get_file();
			}
		}

		if (name_a.is_empty()) {
			name_a = itos(p_a);
		}
	}

	{
		TileSetSource *source = *tile_set->get_source(p_b);

		if (!source->get_name().is_empty()) {
			name_b = source->get_name();
		}

		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		if (atlas_source) {
			Ref<Texture2D> texture = atlas_source->get_texture();
			if (name_b.is_empty() && texture.is_valid()) {
				name_b = texture->get_path().get_file();
			}
		}

		if (name_b.is_empty()) {
			name_b = itos(p_b);
		}
	}

	return NaturalNoCaseComparator()(name_a, name_b);
}

void TilesEditorPlugin::edit(Object *p_object) {
	// Disconnect to changes.
	TileMap *tile_map = Object::cast_to<TileMap>(ObjectDB::get_instance(tile_map_id));
	if (tile_map) {
		tile_map->disconnect("changed", callable_mp(this, &TilesEditorPlugin::_tile_map_changed));
	}

	// Update edited objects.
	tile_set = Ref<TileSet>();
	if (p_object) {
		if (p_object->is_class("TileMap")) {
			tile_map_id = p_object->get_instance_id();
			tile_map = Object::cast_to<TileMap>(ObjectDB::get_instance(tile_map_id));
			tile_set = tile_map->get_tileset();
			EditorNode::get_singleton()->make_bottom_panel_item_visible(tilemap_editor);
		} else if (p_object->is_class("TileSet")) {
			tile_set = Ref<TileSet>(p_object);
			if (tile_map) {
				if (tile_map->get_tileset() != tile_set || !tile_map->is_inside_tree()) {
					tile_map = nullptr;
					tile_map_id = ObjectID();
				}
			}
			EditorNode::get_singleton()->make_bottom_panel_item_visible(tileset_editor);
		}
	}

	// Update the editors.
	_update_editors();

	// Add change listener.
	if (tile_map) {
		tile_map->connect("changed", callable_mp(this, &TilesEditorPlugin::_tile_map_changed));
	}
}

bool TilesEditorPlugin::handles(Object *p_object) const {
	return p_object->is_class("TileMap") || p_object->is_class("TileSet");
}

TilesEditorPlugin::TilesEditorPlugin() {
	set_process_internal(true);

	// Update the singleton.
	singleton = this;

	// Tileset editor.
	tileset_editor = memnew(TileSetEditor);
	tileset_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tileset_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tileset_editor->set_custom_minimum_size(Size2(0, 200) * EDSCALE);
	tileset_editor->hide();

	// Tilemap editor.
	tilemap_editor = memnew(TileMapEditor);
	tilemap_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tilemap_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tilemap_editor->set_custom_minimum_size(Size2(0, 200) * EDSCALE);
	tilemap_editor->hide();

	// Pattern preview generation thread.
	pattern_preview_thread.start(_thread_func, this);

	// Bottom buttons.
	tileset_editor_button = EditorNode::get_singleton()->add_bottom_panel_item(TTR("TileSet"), tileset_editor);
	tileset_editor_button->hide();
	tilemap_editor_button = EditorNode::get_singleton()->add_bottom_panel_item(TTR("TileMap"), tilemap_editor);
	tilemap_editor_button->hide();

	// Initialization.
	_update_editors();
}

TilesEditorPlugin::~TilesEditorPlugin() {
	if (pattern_preview_thread.is_started()) {
		pattern_thread_exit.set();
		pattern_preview_sem.post();
		while (!pattern_thread_exited.is_set()) {
			OS::get_singleton()->delay_usec(10000);
			RenderingServer::get_singleton()->sync(); //sync pending stuff, as thread may be blocked on visual server
		}
		pattern_preview_thread.wait_to_finish();
	}
}
