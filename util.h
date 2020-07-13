#pragma once

#include <chrono>
#include <cgv/data/data_format.h>
#include <cgv/data/data_view.h>
#include <cgv_gl/gl/gl.h>
#include <cgv/math/constants.h>
#include <cgv/render/render_types.h>
#include <cgv/render/texture.h>
#include <cgv/render/frame_buffer.h>
#include <cgv/defines/quote.h>

using namespace cgv::render;

/// type shortcut for 2D unsigned integer vectors
typedef render_types::uvec2 uvec2;
/// type shortcut for 3D unsigned integer vectors
typedef render_types::uvec3 uvec3;
/// type shortcut for 2D integer vectors
typedef render_types::ivec2 ivec2;
/// type shortcut for 3D integer vectors
typedef render_types::ivec3 ivec3;
/// type shortcut for 2D vectors
typedef render_types::vec2 vec2;
/// type shortcut for 3D vectors
typedef render_types::vec3 vec3;
/// type shortcut for 4D vectors
typedef render_types::vec4 vec4;
/// type shortcut for 3x3 matrices
typedef render_types::mat3 mat3;
/// type shortcut for 4x4 matrices
typedef render_types::mat4 mat4;
/// type shortcut for 3D axis aligned bounding boxes
typedef render_types::box3 box3;
/// type shortcut for quaternions
typedef render_types::quat quat;
/// type shortcut for rgb colors
typedef render_types::rgb rgb;
/// type shortcut for rgba colors
typedef render_types::rgba rgba;

namespace util {

	static std::string extend_file_name(std::string file_name) {

		std::string input_dir = QUOTE_SYMBOL_VALUE(INPUT_DIR);
		return input_dir + file_name;
	}

	static std::string resource_file_name(std::string file_name) {

		return extend_file_name("/res/" + file_name);
	}

	template<typename T>
	struct texture_container {
		std::vector<T> data;
		cgv::data::const_data_view view;
		cgv::data::data_format* format = nullptr;
		cgv::render::texture texture;

		~texture_container() {
			delete format;
			format = nullptr;
		}

		void connect(cgv::data::data_format* format) {

			this->format = format;
			view.set_ptr((const void*)data.data());
			view.set_format(format);
		}

		void enable(cgv::render::context& ctx, int unit) {

			texture.enable(ctx, unit);
		}

		void disable(cgv::render::context& ctx) {

			texture.disable(ctx);
		}
	};

	enum CompressionFormat {
		CF_FLT32,
		CF_UINT8
	} cf;

	struct frame_buffer_container {
		CompressionFormat cf;
		cgv::render::frame_buffer fb;
		cgv::render::texture depth;
		cgv::render::texture color;
		cgv::render::texture position;
		cgv::render::texture normal;
		cgv::render::texture depth_mipmap;
		
		bool create_and_validate(cgv::render::context& ctx, unsigned w, unsigned h) {

			fb.create(ctx, w, h);

			std::string fmt = cf == CF_FLT32 ? "flt32" : "uint8";

			depth = cgv::render::texture("uint32[D]");
			color = cgv::render::texture(fmt + "[R,G,B,A]", cgv::render::TF_NEAREST, cgv::render::TF_NEAREST);
			position = cgv::render::texture("flt32[R,G,B]", cgv::render::TF_NEAREST, cgv::render::TF_NEAREST);
			normal = cgv::render::texture(fmt + "[R,G,B]", cgv::render::TF_NEAREST, cgv::render::TF_NEAREST);
			depth_mipmap = cgv::render::texture("flt32[R]", cgv::render::TF_NEAREST, cgv::render::TF_NEAREST_MIPMAP_NEAREST);
			
			depth.create(ctx, cgv::render::TT_2D, w, h);
			color.create(ctx, cgv::render::TT_2D, w, h);
			position.create(ctx, cgv::render::TT_2D, w, h);
			normal.create(ctx, cgv::render::TT_2D, w, h);
			depth_mipmap.create(ctx, cgv::render::TT_2D, w, h);
			depth_mipmap.generate_mipmaps(ctx);
			
			fb.attach(ctx, depth);
			fb.attach(ctx, color, 0, 0);
			fb.attach(ctx, position, 0, 1);
			fb.attach(ctx, normal, 0, 2);

			return fb.is_complete(ctx);
		}

		void destruct(cgv::render::context& ctx) {

			fb.destruct(ctx);
			depth.destruct(ctx);
			position.destruct(ctx);
			color.destruct(ctx);
			normal.destruct(ctx);
			depth_mipmap.destruct(ctx);
		}

		bool ensure(cgv::render::context& ctx) {

			unsigned w = ctx.get_width();
			unsigned h = ctx.get_height();

			if(!fb.is_created() || fb.get_width() != w || fb.get_height() != h) {
				destruct(ctx);
				if(!create_and_validate(ctx, w, h)) {
					std::cerr << "Error: fbo not complete" << std::endl;
					abort();
				}
				return true;
			}

			return false;
		}
	};

	struct color_buffer_container {
		CompressionFormat cf;
		cgv::render::frame_buffer fb;
		cgv::render::texture depth;
		cgv::render::texture color;
		cgv::render::texture alpha_mipmap;
		cgv::render::texture depth_mipmap;
		
		bool create_and_validate(cgv::render::context& ctx, unsigned w, unsigned h) {

			fb.create(ctx, w, h);

			std::string fmt = cf == CF_FLT32 ? "flt32" : "uint8";

			depth = cgv::render::texture("uint32[D]");
			color = cgv::render::texture(fmt + "[R,G,B,A]", cgv::render::TF_NEAREST, cgv::render::TF_NEAREST);
			alpha_mipmap = cgv::render::texture("flt32[R]", cgv::render::TF_NEAREST, cgv::render::TF_NEAREST_MIPMAP_NEAREST);
			depth_mipmap = cgv::render::texture("flt32[R]", cgv::render::TF_NEAREST, cgv::render::TF_NEAREST_MIPMAP_NEAREST);
			
			depth.create(ctx, cgv::render::TT_2D, w, h);
			color.create(ctx, cgv::render::TT_2D, w, h);
			alpha_mipmap.create(ctx, cgv::render::TT_2D, w, h);
			alpha_mipmap.generate_mipmaps(ctx);
			depth_mipmap.create(ctx, cgv::render::TT_2D, w, h);
			depth_mipmap.generate_mipmaps(ctx);
			
			fb.attach(ctx, depth);
			fb.attach(ctx, color, 0, 0);
			
			return fb.is_complete(ctx);
		}

		void destruct(cgv::render::context& ctx) {

			fb.destruct(ctx);
			depth.destruct(ctx);
			color.destruct(ctx);
			alpha_mipmap.destruct(ctx);
			depth_mipmap.destruct(ctx);
		}

		bool ensure(cgv::render::context& ctx) {

			unsigned w = ctx.get_width();
			unsigned h = ctx.get_height();

			if(!fb.is_created() || fb.get_width() != w || fb.get_height() != h) {
				destruct(ctx);
				if(!create_and_validate(ctx, w, h)) {
					std::cerr << "Error: fbo not complete" << std::endl;
					abort();
				}
				return true;
			}

			return false;
		}
	};

	struct timer {
		std::chrono::time_point<std::chrono::steady_clock> begin, end;

		timer() {
			restart();
		}

		void restart() {
			begin = std::chrono::high_resolution_clock::now();
		}

		void stop() {
			end = std::chrono::high_resolution_clock::now();
		}

		double seconds() {
			return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()) / 1000000.0;
		}
	};

	
	float chars2float(char c0, char c1, char c2, char c3) {

		uint32_t uval = 0u;
		uval |= static_cast<uint32_t>(c0);
		uval <<= 8u;
		uval |= static_cast<uint32_t>(c1);
		uval <<= 8u;
		uval |= static_cast<uint32_t>(c2);
		uval <<= 8u;
		uval |= static_cast<uint32_t>(c3);

		float fval = *reinterpret_cast<float*>(&uval);
		return fval;
	}

	unsigned coord2idx(uvec3 coord, uvec3 size) {

		return coord[2] * size[0] * size[1] + coord[1] * size[0] + coord[0];
	}

	uvec3 idx2coord(unsigned idx, uvec3 size) {

		uvec3 coord;
		unsigned a = size[0] * size[1];
		coord[2] = idx / a;
		unsigned b = idx - a * coord[2];
		coord[1] = b / size[0];
		coord[0] = b % size[0];
		return coord;
	}

	struct linear_interpolator {
		std::vector<rgba> values;

		rgba interpolate(float alpha) {

			unsigned count = values.size();

			if(count == 0)
				return rgba(0.0f);
			else if(count == 1)
				return values[0];

			alpha = cgv::math::clamp(alpha, 0.0f, 1.0f);
			

			float fidx = alpha * (float)(count - 1u);
			unsigned idx = (unsigned)fidx;
			float a = cgv::math::clamp(fidx - idx, 0.0f, 1.0f);

			if(idx == count - 1)
				return values[idx];
			
			return (1.0f - a) * values[idx] + a * values[idx + 1];
		}
	};
}
