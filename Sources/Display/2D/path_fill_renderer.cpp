/*
**  ClanLib SDK
**  Copyright (c) 1997-2013 The ClanLib Team
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
**    Mark Page
*/

#include "Display/precomp.h"
#include "path_fill_renderer.h"
#include "API/Display/Render/texture_1d.h"
#include "API/Display/2D/subtexture.h"
#include "API/Display/ImageProviders/png_provider.h"
#include <algorithm>

namespace clan
{
	PathFillRenderer::PathFillRenderer(GraphicContext &gc, RenderBatchBuffer *batch_buffer) : batch_buffer(batch_buffer)
	{
		BlendStateDescription blend_desc;
		blend_desc.set_blend_function(blend_one, blend_one_minus_src_alpha, blend_one, blend_one_minus_src_alpha);
		blend_state = BlendState(gc, blend_desc);
		vertices = (Vertex *)batch_buffer->buffer;
	}

	void PathFillRenderer::set_size(Canvas &canvas, int new_width, int new_height)
	{
		// For simplicity of the code, ensure the mask is always a multiple of mask_block_size
		new_width = mask_block_size * ((new_width + mask_block_size - 1) / mask_block_size);
		new_height = mask_block_size * ((new_height + mask_block_size - 1) / mask_block_size);

		if (width != new_width || height != new_height)
		{
			width = new_width;
			height = new_height;
			scanlines.resize(height * antialias_level);
		}
	}

	void PathFillRenderer::clear()
	{
		for (size_t y = 0; y < scanlines.size(); y++)
		{
			auto &scanline = scanlines[y];
			if (!scanline.edges.empty())
			{
				scanline.edges.clear();
			}
		}
	}

	void PathFillRenderer::end(bool close)
	{
		if (close)
		{
			line(start_x, start_y);
		}
	}

	void PathFillRenderer::line(float x1, float y1)
	{
		float x0 = last_x;
		float y0 = last_y;

		last_x = x1;
		last_y = y1;

		x0 *= static_cast<float>(antialias_level);
		x1 *= static_cast<float>(antialias_level);
		y0 *= static_cast<float>(antialias_level);
		y1 *= static_cast<float>(antialias_level);

		bool up_direction = y1 < y0;
		float dy = y1 - y0;

		const float epsilon = std::numeric_limits<float>::epsilon();
		if (dy < -epsilon || dy > epsilon)
		{
			int start_y = static_cast<int>(std::floor(min(y0, y1) + 0.5f));
			int end_y = static_cast<int>(std::floor(max(y0, y1) - 0.5f)) + 1;

			start_y = max(start_y, 0);
			end_y = min(end_y, height * antialias_level);

			float rcp_dy = 1.0f / dy;

			for (int y = start_y; y < end_y; y++)
			{
				float ypos = y + 0.5f;
				float x = x0 + (x1 - x0) * (ypos - y0) * rcp_dy;
				scanlines[y].edges.push_back(PathScanlineEdge(x, up_direction));
			}
		}
	}

	void PathFillRenderer::fill(Canvas &canvas, PathFillMode mode, const Brush &brush, const Mat4f &transform)
	{
		if (scanlines.empty()) return;
		initialise_buffers(canvas);

		Extent extent = sort_and_find_extent(static_cast<float>(canvas.get_width()));
	
		found_filled_block = false;

		upload_list.clear();

		unsigned char *mask_buffer_data = mask_buffer.get_data_uint8();
		int mask_buffer_pitch = mask_buffer.get_pitch();

		PathRasterRange range[scanline_block_size];

		for (size_t y = 0; y < scanlines.size(); y += scanline_block_size)
		{
			auto &scanline = scanlines[y];
			if (scanline.edges.empty())
				continue;

			for (unsigned int cnt = 0; cnt < scanline_block_size; cnt++)
			{
				range[cnt].begin(&scanlines[y + cnt], mode);
			}

			for (int xpos = extent.left; xpos < extent.right; xpos += scanline_block_size)
			{
				int block_x = (next_block * mask_block_size) % mask_texture_size;
				int block_y = ((next_block * mask_block_size) / mask_texture_size)* mask_block_size;

				// Identify filled blocks
				bool full_block = true;
				for (unsigned int filled_cnt = 0; filled_cnt < scanline_block_size; filled_cnt++)
				{
					if (!range[filled_cnt].found)
					{
						full_block = false;
						break;
					}
					if ((range[filled_cnt].x0 > xpos) || (range[filled_cnt].x1 < (xpos + scanline_block_size)))
					{
						full_block = false;
						break;
					}
				}

				if (full_block)
				{
					if (!found_filled_block)
					{
						found_filled_block = true;
						filled_block_index = next_block;
					}
					else
					{
						upload_list.push_back(Block(Point(xpos / antialias_level, y / antialias_level), filled_block_index));
						continue;
					}
				}


				bool empty_block = true;
				for (unsigned int cnt = 0; cnt < scanline_block_size; cnt++)
				{
					unsigned char *line = mask_buffer_data + mask_buffer_pitch * (block_y + cnt / antialias_level) + block_x;
					if (range[cnt].found)
					{
						empty_block = false;
					}

					while (range[cnt].found)
					{
						int x0 = static_cast<int>(range[cnt].x0 + 0.5f);
						if (x0 >= xpos + scanline_block_size)
							break;
						int x1 = static_cast<int>(range[cnt].x1 - 0.5f) + 1;

						x0 = max(x0, xpos);
						x1 = min(x1, xpos + scanline_block_size);

						if (x0 >= x1)	// Done segment
						{
							range[cnt].next();
						}
						else
						{
							for (int x = x0 - xpos; x < x1 - xpos; x++)
							{
								int pixel = line[x / antialias_level];
								pixel = min(pixel + (256 / (antialias_level*antialias_level)), 255);
								line[x / antialias_level] = pixel;
							}
							range[cnt].x0 = x1;	// For next time
						}
					}
				}
				if (!empty_block)
				{
					upload_list.push_back(Block(Point(xpos / antialias_level, y / antialias_level), next_block));
					next_block++;
				}
				// Check for max vertices or full mask
				if ((next_block == max_blocks) || (((upload_list.size() + 1) * 6) >= max_vertices))
				{
						store_vertices(canvas, brush, transform);
						flush(canvas);
						initialise_buffers(canvas);
						mask_buffer_data = mask_buffer.get_data_uint8();
						mask_buffer_pitch = mask_buffer.get_pitch();
						upload_list.clear();
						found_filled_block = false;
				}
			}
		}
		//PNGProvider::save(mask_buffer, "c:\\development\\test.png");

		store_vertices(canvas, brush, transform);
	}

	PathFillRenderer::Extent PathFillRenderer::sort_and_find_extent(float canvas_width)
	{
		float width = canvas_width*static_cast<float>(antialias_level);

		Extent extent(width, 0.0f);		// Dummy initial values

		// Precalculation to determine the extents
		for (size_t y = 0; y < scanlines.size(); y++)
		{
			auto &scanline = scanlines[y];
			if (scanline.edges.empty())
				continue;

			std::sort(scanline.edges.begin(), scanline.edges.end(), [](const PathScanlineEdge &a, const PathScanlineEdge &b) { return a.x < b.x; });

			// Calculate extent
			if (scanline.edges[0].x < extent.left)
				extent.left = scanline.edges[0].x;

			if (scanline.edges[scanline.edges.size() - 1].x > extent.right)
				extent.right = scanline.edges[scanline.edges.size() - 1].x;
		}
		// Clip extents
		if (extent.right > width)
			extent.right = width;
		if (extent.left < 0)
			extent.left = 0;

		return extent;
	}

	void PathFillRenderer::flush(GraphicContext &gc)
	{
		if (position <= 0)		// Nothing to flush
			return;

		mask_buffer.unlock();
		instance_buffer.unlock();

		int gpu_index;
		VertexArrayVector<Vertex> gpu_vertices(batch_buffer->get_vertex_buffer(gc, gpu_index));

		if (prim_array[gpu_index].is_null())
		{
			prim_array[gpu_index] = PrimitivesArray(gc);
			prim_array[gpu_index].set_attributes(0, gpu_vertices, cl_offsetof(Vertex, Position));
			prim_array[gpu_index].set_attributes(1, gpu_vertices, cl_offsetof(Vertex, BrushData1));
			prim_array[gpu_index].set_attributes(2, gpu_vertices, cl_offsetof(Vertex, BrushData2));
			prim_array[gpu_index].set_attributes(3, gpu_vertices, cl_offsetof(Vertex, TexCoord0));
			prim_array[gpu_index].set_attributes(4, gpu_vertices, cl_offsetof(Vertex, Mode));

		}

		gpu_vertices.upload_data(gc, 0, vertices, position);

		size_t blocks_height = (upload_list.size() + mask_texture_size - 1) / mask_texture_size * mask_texture_size;
		int block_y = (((next_block-1) * mask_block_size) / mask_texture_size)* mask_block_size;
		mask_texture.set_subimage(gc, 0, 0, mask_buffer, Rect(Point(0, 0), Size(mask_texture_size, block_y + mask_block_size)));

		instance_texture.set_subimage(gc, 0, 0, instance_buffer, Rect(Point(0, 0), Size(current_gradient_position * instance_buffer_gradient_stop_size, 1)));

		gc.set_blend_state(blend_state);
		gc.set_program_object(program_path);
		gc.set_texture(0, mask_texture);
		gc.set_texture(1, instance_texture);
		if (!current_texture.is_null())
			gc.set_texture(2, current_texture);
		gc.draw_primitives(type_triangles, position, prim_array[gpu_index]);
		if (!current_texture.is_null())
		{
			gc.reset_texture(2);
			current_texture = Texture2D();
		}
		gc.reset_texture(1);
		gc.reset_texture(0);
		gc.reset_program_object();
		gc.reset_blend_state();
		position = 0;
		current_gradient_position = 0;

		// Finishedwith the buffers
		mask_buffer = TransferTexture();
		mask_texture = Texture2D();
		instance_buffer = TransferTexture();
		instance_texture = Texture2D();

		batch_buffer->set_transfer_r8_used(mask_buffer_id, block_y + mask_block_size);
		next_block = 0;
		upload_list.clear();
	}

	void PathFillRenderer::store_vertices(Canvas &canvas, const Brush &brush, const Mat4f &transform)
	{
		int num_vertices = upload_list.size() * 6;
		if (position + num_vertices > max_vertices)
			canvas.flush();

		if (num_vertices > max_vertices)
			throw Exception("Too many vertices for PathFillRenderer");

		GraphicContext gc = canvas.get_gc();

		unsigned int num_stops = brush.stops.size();

		if (num_stops > max_gradient_stops)
			throw Exception("Too many gradient stops for PathFillRenderer");

		if (current_gradient_position + num_stops > max_gradient_stops)
			canvas.flush();

		float rcp_canvas_width_x2 = 2.0f / static_cast<float>(canvas.get_width());
		float rcp_canvas_height_x2 = 2.0f / static_cast<float>(canvas.get_height());

		Mat3f inv_brush_transform;
		Mat4f inv_transform;

		Vec4f brush_data2;
		int draw_mode;
		Vec4f brush_data1_bottom_left;
		Vec4f brush_data1_bottom_right;
		Vec4f brush_data1_top_left;
		Vec4f brush_data1_top_right;
		Pointf start_point;
		Pointf center_point;

		// ******* Instance *******
		if (brush.type == BrushType::linear)
		{
			draw_mode = 1;
			Pointf end_point = transform_point(brush.end_point, brush.transform, transform);
			start_point = transform_point(brush.start_point, brush.transform, transform);
			Pointf dir = end_point - start_point;
			Pointf dir_normed = Pointf::normalize(dir);
			brush_data1_bottom_left.set_zw(dir_normed);
			brush_data1_bottom_right.set_zw(dir_normed);
			brush_data1_top_left.set_zw(dir_normed);
			brush_data1_top_right.set_zw(dir_normed);

			brush_data2.x = 1.0f / dir.length();
			brush_data2.y = current_gradient_position;
			brush_data2.z = current_gradient_position + num_stops;
		}
		else if (brush.type == BrushType::radial)
		{
			center_point = transform_point(brush.center_point, brush.transform, transform);
			Pointf radius = transform_point(Pointf(brush.radius_x, brush.radius_y), brush.transform, transform) - transform_point(Pointf(), brush.transform, transform);

			draw_mode = 2;
			brush_data2.x = 1.0f / brush.radius_x;
			brush_data2.y = current_gradient_position;
			brush_data2.z = current_gradient_position + num_stops;
		}
		else if (brush.type == BrushType::image)
		{
			draw_mode = 3;
			Subtexture subtexture = brush.image.get_texture();
			if (subtexture.is_null())
				return;

			if (!current_texture.is_null())		// Currently only support a single path texture
			{
				if (subtexture.get_texture() != current_texture)
				{
					flush(gc);
				}
			}

			current_texture = subtexture.get_texture();
			if (current_texture.is_null())
				throw Exception("BrushType::image used without a valid texture");

			inv_brush_transform = Mat3f::inverse(brush.transform);
			inv_transform = Mat4f::inverse(transform);

			Rectf src = subtexture.get_geometry();
		}
		else
		{
			draw_mode = 0;
			brush_data2 = brush.color;
		}

		// ******* Vertex *******
		for (unsigned int upload_index = 0; upload_index < upload_list.size(); upload_index++)
		{
			Rectf upload_rect(Pointf(upload_list[upload_index].output_position), Sizef(mask_block_size, mask_block_size));

			if (brush.type == BrushType::linear)
			{
				brush_data1_bottom_left.set_xy(upload_rect.get_bottom_left() - start_point);
				brush_data1_bottom_right.set_xy(upload_rect.get_bottom_right() - start_point);
				brush_data1_top_left.set_xy(upload_rect.get_top_left() - start_point);
				brush_data1_top_right.set_xy(upload_rect.get_top_right() - start_point);
			}
			else if (brush.type == BrushType::radial)
			{
				brush_data1_bottom_left.set_xy(upload_rect.get_bottom_left() - center_point);
				brush_data1_bottom_right.set_xy(upload_rect.get_bottom_right() - center_point);
				brush_data1_top_left.set_xy(upload_rect.get_top_left() - center_point);
				brush_data1_top_right.set_xy(upload_rect.get_top_right() - center_point);
			}
			else if (brush.type == BrushType::image)
			{
				Subtexture subtexture = brush.image.get_texture();
				Rectf src = subtexture.get_geometry();

				// Find transformed UV coordinates for image covering the entire mask texture:
				Pointf image_tl = transform_point(upload_rect.get_top_left(), inv_brush_transform, inv_transform);
				Pointf image_tr = transform_point(upload_rect.get_top_right(), inv_brush_transform, inv_transform);
				Pointf image_bl = transform_point(upload_rect.get_bottom_left(), inv_brush_transform, inv_transform);
				Pointf image_br = transform_point(upload_rect.get_bottom_right(), inv_brush_transform, inv_transform);

				// Convert to subtexture coordinates:
				Sizef tex_size = Sizef((float)current_texture.get_width(), (float)current_texture.get_height());

				brush_data1_bottom_left.set_xy(Vec2f((src.left + image_bl.x) / tex_size.width, (src.top + image_bl.y) / tex_size.height));
				brush_data1_bottom_right.set_xy(Vec2f((src.left + image_br.x) / tex_size.width, (src.top + image_br.y) / tex_size.height));
				brush_data1_top_left.set_xy(Vec2f((src.left + image_tl.x) / tex_size.width, (src.top + image_tl.y) / tex_size.height));
				brush_data1_top_right.set_xy(Vec2f((src.left + image_tr.x) / tex_size.width, (src.top + image_tr.y) / tex_size.height));
			}
			else
			{
			}

			Rectf upload_rect_normalised(upload_rect);
			upload_rect_normalised.left = (upload_rect_normalised.left * rcp_canvas_width_x2) - 1.0f;
			upload_rect_normalised.right = (upload_rect_normalised.right * rcp_canvas_width_x2) - 1.0f;
			upload_rect_normalised.top = (upload_rect_normalised.top * rcp_canvas_height_x2) - 1.0f;
			upload_rect_normalised.bottom = (upload_rect_normalised.bottom * rcp_canvas_height_x2) - 1.0f;

			int block_index = upload_list[upload_index].mask_index;
			int block_x = (block_index* mask_block_size) % mask_texture_size;
			int block_y = ((block_index* mask_block_size) / mask_texture_size) * mask_block_size;
			Rectf block_normalised;
			block_normalised.left = block_x * rcp_mask_texture_size;
			block_normalised.right = (block_x + mask_block_size) * rcp_mask_texture_size;
			block_normalised.top = block_y * rcp_mask_texture_size;
			block_normalised.bottom = (block_y + mask_block_size) * rcp_mask_texture_size;

			vertices[position + 0] = Vertex(Vec4f(upload_rect_normalised.left, -upload_rect_normalised.top, 0.0f, 1.0f), brush_data1_top_left, brush_data2, Vec2f(block_normalised.left, block_normalised.top), draw_mode);
			vertices[position + 1] = Vertex(Vec4f(upload_rect_normalised.right, -upload_rect_normalised.top, 0.0f, 1.0f), brush_data1_top_right, brush_data2, Vec2f(block_normalised.right, block_normalised.top), draw_mode);
			vertices[position + 2] = Vertex(Vec4f(upload_rect_normalised.left, -upload_rect_normalised.bottom, 0.0f, 1.0f), brush_data1_bottom_left, brush_data2, Vec2f(block_normalised.left, block_normalised.bottom), draw_mode);
			vertices[position + 3] = Vertex(Vec4f(upload_rect_normalised.right, -upload_rect_normalised.top, 0.0f, 1.0f), brush_data1_top_right, brush_data2, Vec2f(block_normalised.right, block_normalised.top), draw_mode);
			vertices[position + 4] = Vertex(Vec4f(upload_rect_normalised.right, -upload_rect_normalised.bottom, 0.0f, 1.0f), brush_data1_bottom_right, brush_data2, Vec2f(block_normalised.right, block_normalised.bottom), draw_mode);
			vertices[position + 5] = Vertex(Vec4f(upload_rect_normalised.left, -upload_rect_normalised.bottom, 0.0f, 1.0f), brush_data1_bottom_left, brush_data2, Vec2f(block_normalised.left, block_normalised.bottom), draw_mode);
			position += 6;
		}

		GradientStops *instance_ptr = instance_buffer.get_data<GradientStops>() + current_gradient_position;
		for (unsigned int cnt = 0; cnt < num_stops; cnt++)
		{
			*(instance_ptr++) = GradientStops(brush.stops[cnt].color, brush.stops[cnt].position);
		}
		current_gradient_position += num_stops;
	}

	inline Pointf PathFillRenderer::transform_point(Pointf point, const Mat3f &brush_transform, const Mat4f &transform) const
	{
		point = Pointf(
			brush_transform.matrix[0 * 3 + 0] * point.x + brush_transform.matrix[1 * 3 + 0] * point.y + brush_transform.matrix[2 * 3 + 0],
			brush_transform.matrix[0 * 3 + 1] * point.x + brush_transform.matrix[1 * 3 + 1] * point.y + brush_transform.matrix[2 * 3 + 1]);

		return Pointf(
			transform.matrix[0 * 4 + 0] * point.x + transform.matrix[1 * 4 + 0] * point.y + transform.matrix[3 * 4 + 0],
			transform.matrix[0 * 4 + 1] * point.x + transform.matrix[1 * 4 + 1] * point.y + transform.matrix[3 * 4 + 1]);
	}


	void PathFillRenderer::initialise_buffers(Canvas &canvas)
	{
		if (mask_texture.is_null())
		{
			GraphicContext gc = canvas.get_gc();
			mask_texture = batch_buffer->get_texture_r8(gc);
			mask_buffer = batch_buffer->get_transfer_r8(gc, mask_buffer_id, access_read_write);
			instance_texture = batch_buffer->get_texture_rgba32f(gc);
			instance_buffer = batch_buffer->get_transfer_rgba32f(gc, access_write_only);
		}
	}
	/////////////////////////////////////////////////////////////////////////////

	void PathRasterRange::begin(const PathScanline *new_scanline, PathFillMode new_mode)
	{
		scanline = new_scanline;
		mode = new_mode;
		found = false;
		x0 = 0.0f;
		x1 = 0.0f;
		i = 0;
		nonzero_rule = 0;

		next();
	}

	void PathRasterRange::next()
	{
		if (i + 1 >= scanline->edges.size())
		{
			found = false;
			return;
		}

		if (mode == PathFillMode::alternate)
		{
			x0 = scanline->edges[i].x;
			x1 = scanline->edges[i + 1].x;

			i += 2;
			found = true;
		}
		else
		{
			x0 = scanline->edges[i].x;
			nonzero_rule += scanline->edges[i].up_direction ? 1 : -1;
			i++;

			while (i < scanline->edges.size())
			{
				nonzero_rule += scanline->edges[i].up_direction ? 1 : -1;
				x1 = scanline->edges[i].x;
				i++;

				if (nonzero_rule == 0)
				{
					found = true;
					return;
				}
			}
			found = false;
		}
	}


}
