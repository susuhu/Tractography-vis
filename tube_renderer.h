#pragma once

#include <cgv/render/context.h>
#include <cgv/render/shader_program.h>
#include <cgv/render/vertex_buffer.h>
#include <cgv/render/attribute_array_binding.h>
#include <cgv_gl/gl/gl_context.h>
#include <cgv/media/illum/textured_surface_material.h>

using namespace cgv::render;

/// define a render style for tubes
struct tube_render_style : public render_types {

	/// default value for color when map color to material is used
	cgv::media::illum::surface_material::color_type surface_color;
	/// culling mode for point splats, set to CM_OFF in constructor
	CullingMode culling_mode;
	/// illumination mode defaults to \c IM_ONE_SIDED
	IlluminationMode illumination_mode;
	/// material side[s] where color is to be mapped to the diffuse material component, defaults to MS_FRONT_AND_BACK
	unsigned map_color_to_material;
	/// material of surface
	cgv::media::illum::textured_surface_material material;

	/// multiplied to the sphere radii, initialized to 1
	float radius_scale;
	/// default value assigned to radius attribute in \c enable method of tube renderer, set to 1 in constructor
	float radius;

	bool enable_ambient_occlusion;
	float ao_offset;
	float ao_distance;
	float ao_strength;

	vec3 tex_offset;
	vec3 tex_scaling;
	vec3 tex_coord_scaling;
	float texel_size;
	float cone_angle_factor;
	std::vector<vec3> sample_dirs;
	/// construct with default values
	tube_render_style();
};

/// renderer that supports tube raycasting
class tube_renderer : public render_types {
protected:
	/// dummy vao used for cover screen shader
	GLuint dummy_vao;
	/// attribue array binding used to store array pointers
	attribute_array_binding aab;
	/// store vertex buffers generated per attribute location
	std::map<int, vertex_buffer*> vbos;
	/// shader program used to rasterize the primitives
	shader_program rasterize_prog;
	/// shader program used to apply shading and ambient occlusion effects
	shader_program shading_prog;
	/// track whether position attribute is defined
	bool has_positions;
	/// track whether color attribute is defined
	bool has_colors;
	/// whether the radius is set individually for each position
	bool has_radii;
	/// whether the shader should be rebuilt after a define update
	std::string shader_defines;
	/// default render style
	tube_render_style* default_render_style;
	/// current render style, can be set by user
	const tube_render_style* trs;
	/// overload to allow instantiation of tube_renderer
	tube_render_style* create_render_style() const;
	///
	vec3 eye_position;
	///
	vec3 view_direction;
	///
	template <typename T>
	bool set_attribute_array(const context& ctx, int loc, const T& array) {

		bool res;
		vertex_buffer*& vbo_ptr = vbos[loc];
		if(vbo_ptr) {
			if(vbo_ptr->get_size_in_bytes() == array_descriptor_traits<T>::get_size(array))
				res = vbo_ptr->replace(ctx, 0, array_descriptor_traits<T>::get_address(array), array_descriptor_traits<T>::get_nr_elements(array));
			else {
				vbo_ptr->destruct(ctx);
				res = vbo_ptr->create(ctx, array);
			}
		} else {
			vbo_ptr = new vertex_buffer();
			res = vbo_ptr->create(ctx, array);
		}
		if(res)
			res = aab.set_attribute_array(ctx, loc, array_descriptor_traits<T>::get_type_descriptor(array), *vbo_ptr, 0, array_descriptor_traits<T>::get_nr_elements(array));
		return res;
	}
	///
	std::string build_define_string();
	///
	bool build_shader(context& ctx, std::string defines = "");

public:
	/// initializes members
	tube_renderer();
	///
	void destruct(const context& ctx);
	/// construct shader programs and return whether this was successful, call inside of init method of drawable
	bool init(context& ctx);
	///
	void set_eye_position(const vec3& _eye_position) {

		eye_position = _eye_position;
	}
	///
	void set_view_direction(const vec3& _view_direction) {

		view_direction = normalize(_view_direction);
	}
	/// reference given render style
	void set_render_style(const tube_render_style& _trs) {

		trs = &_trs;
	}
	/// method to set the position attribute from a vector of positions of type vec3
	void set_position_array(const context& ctx, const std::vector<vec3>& positions) {

		has_positions = true;
		set_attribute_array(ctx, rasterize_prog.get_attribute_location(ctx, "position"), positions);
	}
	/// method to set the color attribute from a vector of colors of type rgba
	void set_color_array(const context& ctx, const std::vector<rgba>& colors) {

		has_colors = true;
		set_attribute_array(ctx, rasterize_prog.get_attribute_location(ctx, "color"), colors);
	}
	/// method to set the radius attribute from a vector of radii of type float
	void set_radius_array(const context& ctx, const std::vector<float>& radii) {

		has_radii = true;
		set_attribute_array(ctx, rasterize_prog.get_attribute_location(ctx, "radius"), radii);
	}
	/// returns the OpenGL handle to the specified buffer of -1 if the buffer does not exist
	int get_vbo(const context& ctx, const std::string attr_name);
	///
	bool enable(context& ctx);
	///
	bool disable(context& ctx);
	///
	void rasterize(context& ctx, GLsizei count);
	///
	void shade(context& ctx);
};
