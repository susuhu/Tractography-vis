#include "tube_renderer.h"
#include <cgv_gl/gl/gl.h>
#include <cgv_gl/gl/gl_tools.h>

tube_render_style::tube_render_style() : material("default") {

	surface_color = cgv::media::illum::surface_material::color_type(0.4f, 0.1f, 0.7f);
	culling_mode = CM_OFF;
	illumination_mode = IM_ONE_SIDED;
	map_color_to_material = 3;
	material.ref_brdf_type() = cgv::media::illum::BrdfType(cgv::media::illum::BT_STRAUSS_DIFFUSE + cgv::media::illum::BT_STRAUSS);



	radius = 1.0f;
	radius_scale = 1.0f;

	enable_ambient_occlusion = false;
	ao_offset = 0.04f;
	ao_distance = 0.8f;
	ao_strength = 1.0f;

	tex_offset = vec3(0.0f);
	tex_scaling = vec3(1.0f);
	tex_coord_scaling = vec3(1.0f);
	texel_size = 1.0f;

	cone_angle_factor = 1.0f;
	sample_dirs.resize(3);
	sample_dirs[0] = vec3(0.0f, 1.0f, 0.0f);
	sample_dirs[1] = vec3(0.0f, 1.0f, 0.0f);
	sample_dirs[2] = vec3(0.0f, 1.0f, 0.0f);
}

tube_render_style* tube_renderer::create_render_style() const {

	return new tube_render_style();
}

tube_renderer::tube_renderer() {

	has_positions = false;
	has_colors = false;
	has_radii = false;

	trs = nullptr;
	default_render_style = nullptr;
	shader_defines = "";

	glGenBuffers(1, &dummy_vao);
}

void tube_renderer::destruct(const context& ctx) {

	aab.destruct(ctx);
	vbos.clear();

	has_positions = false;
	has_colors = false;
	has_radii = false;

	rasterize_prog.destruct(ctx);
	shading_prog.destruct(ctx);

	if(default_render_style)
		delete default_render_style;
	default_render_style = nullptr;
	
	if(dummy_vao > 0)
		glDeleteBuffers(1, &dummy_vao);
	dummy_vao = 0;
}

bool tube_renderer::init(cgv::render::context& ctx) {

	if(!default_render_style) {
		default_render_style = create_render_style();
	}
	if(!trs)
		trs = default_render_style;
	bool res = default_render_style != 0;

	res = aab.create(ctx) && res;

	if(!rasterize_prog.is_created() || shading_prog.is_created()) {
		res = build_shader(ctx, build_define_string()) && res;
	}
	return res;
}

std::string tube_renderer::build_define_string() {

	std::string defines = "ENABLE_AMBIENT_OCCLUSION=";
	defines += std::to_string((int)trs->enable_ambient_occlusion);
	
	return defines;
}

bool tube_renderer::build_shader(context& ctx, std::string defines) {

	shader_defines = defines;

	if(rasterize_prog.is_created())
		rasterize_prog.destruct(ctx);

	if(!rasterize_prog.is_created()) {
		if(!rasterize_prog.build_program(ctx, "tube.glpr", true, defines)) {
			std::cerr << "ERROR in tube_renderer::init() ... could not build program tube.glpr" << std::endl;
			return false;
		}
	}

	if(shading_prog.is_created())
		shading_prog.destruct(ctx);

	if(!shading_prog.is_created()) {
		if(!shading_prog.build_program(ctx, "tube_shading.glpr", true, defines)) {
			std::cerr << "ERROR in tube_renderer::init() ... could not build program tube_shading.glpr" << std::endl;
			return false;
		}
	}

	return true;
}

int tube_renderer::get_vbo(const context& ctx, const std::string attr_name) {

	
	int loc = rasterize_prog.get_attribute_location(ctx, attr_name);
	auto it = vbos.find(loc);
	if(it != vbos.end()) {
		vertex_buffer* vbo_ptr = vbos[loc];
		if(vbo_ptr->handle) {
			return (const int&)vbo_ptr->handle - 1;
		}
	}
	
	return (const int&)-1;
}

bool tube_renderer::enable(context& ctx) {

	std::string defines = build_define_string();
	if(defines != shader_defines) {
		if(!build_shader(ctx, defines))
			return false;
	}

	if(!rasterize_prog.is_linked() || !shading_prog.is_linked())
		return false;

	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);
	//glDisable(GL_CULL_FACE);
	
	bool res = aab.enable(ctx);
	return true;
}

bool tube_renderer::disable(context& ctx) {

	glDisable(GL_CULL_FACE);

	bool res = aab.disable(ctx);
	return res;
}

void tube_renderer::rasterize(context& ctx, GLsizei count) {

	rasterize_prog.enable(ctx);
	if(!has_radii)
		rasterize_prog.set_attribute(ctx, "radius", trs->radius);
	rasterize_prog.set_uniform(ctx, "radius_scale", trs->radius_scale);
	rasterize_prog.set_uniform(ctx, "eye_pos", eye_position);
	rasterize_prog.set_uniform(ctx, "view_dir", view_direction);
	
	glDrawArrays(GL_LINES, (GLint)0, count);

	rasterize_prog.disable(ctx);
}

void tube_renderer::shade(context& ctx) {

	shading_prog.enable(ctx);
	ctx.set_material(trs->material);
	ctx.set_color(trs->surface_color);
	shading_prog.set_uniform(ctx, "map_color_to_material", int(trs->map_color_to_material));
	shading_prog.set_uniform(ctx, "culling_mode", int(trs->culling_mode));
	shading_prog.set_uniform(ctx, "illumination_mode", int(trs->illumination_mode));
	shading_prog.set_uniform(ctx, "viewport_dims", ivec2(ctx.get_width(), ctx.get_height()));

	if(trs->enable_ambient_occlusion) {
		shading_prog.set_uniform(ctx, "ao_offset", trs->ao_offset);
		shading_prog.set_uniform(ctx, "ao_distance", trs->ao_distance);
		shading_prog.set_uniform(ctx, "ao_strength", trs->ao_strength);
		shading_prog.set_uniform(ctx, "density_tex_offset", trs->tex_offset);
		shading_prog.set_uniform(ctx, "density_tex_scaling", trs->tex_scaling);
		shading_prog.set_uniform(ctx, "tex_coord_scaling", trs->tex_coord_scaling);
		shading_prog.set_uniform(ctx, "texel_size", trs->texel_size);
		shading_prog.set_uniform(ctx, "cone_angle_factor", trs->cone_angle_factor);
		shading_prog.set_uniform_array(ctx, "sample_dirs", trs->sample_dirs);
	}

	mat4 MV(ctx.get_modelview_matrix());
	mat3 NM;
	NM(0, 0) = MV(0, 0);
	NM(0, 1) = MV(0, 1);
	NM(0, 2) = MV(0, 2);
	NM(1, 0) = MV(1, 0);
	NM(1, 1) = MV(1, 1);
	NM(1, 2) = MV(1, 2);
	NM(2, 0) = MV(2, 0);
	NM(2, 1) = MV(2, 1);
	NM(2, 2) = MV(2, 2);
	NM.transpose();

	shading_prog.set_uniform(ctx, "inverse_modelview_mat", inv(MV));
	shading_prog.set_uniform(ctx, "inverse_normal_mat", NM);

	glDisable(GL_DEPTH_TEST);
	glBindVertexArray(dummy_vao);

	glDrawArrays(GL_TRIANGLE_STRIP, (GLint)0, (GLsizei)4);

	glBindVertexArray(0);
	glEnable(GL_DEPTH_TEST);

	shading_prog.disable(ctx);
}

#include <cgv/gui/provider.h>

namespace cgv {
	namespace gui {

		struct tube_render_style_gui_creator : public gui_creator {

			/// attempt to create a gui and return whether this was successful
			bool create(provider* p, const std::string& label, void* value_ptr, const std::string& value_type, const std::string& gui_type, const std::string& options, bool*) {

				if(value_type != cgv::type::info::type_name<tube_render_style>::get_name())
					return false;

				tube_render_style* trs_ptr = reinterpret_cast<tube_render_style*>(value_ptr);
				cgv::base::base* b = dynamic_cast<cgv::base::base*>(p);

				// Tube parameters
				p->add_member_control(b, "default radius", trs_ptr->radius, "value_slider", "min=0.001;step=0.0001;max=10.0;log=true;ticks=true");
				p->add_member_control(b, "radius scale", trs_ptr->radius_scale, "value_slider", "min=0.01;step=0.0001;max=100.0;log=true;ticks=true");

				// Ambient occlusion parameters
				p->add_decorator("ambient occlusion", "heading", "level=4");
				p->add_member_control(b, "enable", trs_ptr->enable_ambient_occlusion, "check");
				p->add_member_control(b, "ao offset", trs_ptr->ao_offset, "value_slider", "min=0.0;step=0.0001;max=0.2;log=true;ticks=true");
				p->add_member_control(b, "ao distance", trs_ptr->ao_distance, "value_slider", "min=0.0;step=0.0001;max=1.0;log=true;ticks=true");
				p->add_member_control(b, "ao strength", trs_ptr->ao_strength, "value_slider", "min=0.0;step=0.0001;max=10.0;log=true;ticks=true");

				// Surface paramaters
				p->add_member_control(b, "illumination_mode", trs_ptr->illumination_mode, "dropdown", "enums='off,onesided,twosided'");
				if(p->begin_tree_node("color and materials", trs_ptr->surface_color, false, "level=3")) {
					p->align("\a");
					p->add_member_control(b, "surface_color", trs_ptr->surface_color);
					if(p->begin_tree_node("material", trs_ptr->material, false, "level=3")) {
						p->align("\a");
						p->add_gui("front_material", trs_ptr->material);
						p->align("\b");
						p->end_tree_node(trs_ptr->material);
					}
					p->align("\b");
					p->end_tree_node(trs_ptr->surface_color);
				}
				return true;
			}
		};

#include <cgv_gl/gl/lib_begin.h>

		cgv::gui::gui_creator_registration<tube_render_style_gui_creator> tube_rs_gc_reg("tube_render_style_gui_creator");
	}
}
