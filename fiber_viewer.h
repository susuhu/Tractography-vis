/*
	The source code of the fiber_viewer plugin is part of the complex lab for fiber tractography visualization.
	The code serves as a baseline to start implementing the given tasks and may be changed anywhere necessary.
	Usage of this code for purposes other than the complex lab is not allowed.
*/

#pragma once

#include <cgv/base/node.h>
#include <cgv/render/drawable.h>
#include <cgv/gui/provider.h>
#include <cgv/gui/event_handler.h>
#include <cgv/render/render_types.h>
#include <cgv/render/shader_program.h>
#include <cgv_gl/volume_renderer.h>
#include <random>


#include "util.h"
#include "tube_renderer.h"
#include "gpu_sorter.h"

#include "nifti1.h"
#include "znzlib.h"
#include "nifti1_io.h"


class fiber_viewer :
	public cgv::base::node, 
	public cgv::render::drawable, 
	public cgv::gui::provider, 
	public cgv::gui::event_handler
{
protected:

	std::string dataset_filename;

	enum RenderMode {
		RM_DEFERRED,
		RM_TRANSPARENT_NAIVE,
		RM_TRANSPARENT_ATOMIC_LOOP,
		RM_VOLUME
	} render_mode;

	enum ColorSource {
		CS_ATTRIBUTE,
		CS_MIDPOINT,
		CS_SEGMENT,
		CS_COOLWARM,
		CS_EXTENDED_KINDLMANN,
		CS_BLACKBODY,
		CS_EXTENDED_BLACKBODY,
		CS_ISORAINBOW,
		CS_BOYS,

	} color_source;

	enum AtomicLoopScratchSize {
		ALSS_1 = 1,
		ALSS_2 = 2,
		ALSS_4 = 4,
		ALSS_8 = 8,
		ALSS_16 = 16,
		ALSS_32 = 32
	} alss;

	enum VoxelResolution {
		VR_8,
		VR_16,
		VR_32,
		VR_64,
		VR_128,
		VR_256,
		VR_512
	} voxel_resolution;

	double check_for_click;
	bool do_change_dataset;
	bool do_change_color_source;
	bool do_create_density_volume;
	bool do_rebuild_framebuffer;
	bool do_rebuild_buffers;

	bool disable_sorting;
	bool disable_clipping;

	struct tract {
		unsigned offset = 0u;
		unsigned size = 0u;
	};

	// Raw data
	box3 dataset_bbox;
	std::vector<tract> tracts;
	std::vector<vec3> raw_positions;
	std::vector<float> raw_radii;
	std::vector<float> raw_attributes;

	// Prepared data
	std::vector<vec3> positions;
	std::vector<float> radii;
	std::vector<rgba> colors_midpoint;
	std::vector<rgba> colors_segment;
	std::vector<rgba> colors_attribute;
	std::vector<rgba> colors_coolwarm;
	std::vector<rgba> colors_extended_kindlmann;
	std::vector<rgba> colors_blackbody;
	std::vector<rgba> colors_extended_blackbody;
	std::vector<rgba> colors_isorainbow;
	std::vector<rgba> colors_boys;


	texture tf_tex;
	std::string resource_path;


	// Render members
	cgv::render::view* view_ptr;
	tube_renderer tr;
	tube_render_style tstyle;
	volume_render_style vstyle;
	gpu_sorter sorter;
	util::frame_buffer_container fb;
	util::color_buffer_container cb;

	util::texture_container<float> density_tex;
	util::texture_container<float> fa_tex;

	GLuint segment_ibo;
	GLuint ibo;
	GLuint positions_ssbo;
	GLuint radii_ssbo;
	GLuint colors_ssbo;
	GLuint scratch_buffer;

	rgba background_color;
	util::linear_interpolator color_map;
	vec3 dataset_center;
	float alpha_scale;
	
	// Shader programs
	shader_program tube_transparent_naive_prog;
	//shader_program tube_transparent_al_prog;
	//shader_program tube_transparent_al_blend_prog;

	shader_program final_blend_prog;
	shader_program expand_indices_prog;
	shader_program clear_ssbo_prog;

	bool generate_test_dataset();
	bool read_trk_file(std::string file);

	void set_dataset(context& ctx, bool generate_test = true);
	void prepare_data(context& ctx);
	void create_density_volume(const context& ctx, const box3 bbox, const float radius);

	void set_color_source(const context& ctx);
	bool load_shader(context& ctx, shader_program& prog, std::string name, std::string defines = "");
	void create_buffers(const context& ctx);
	void sort(context& ctx, const GLuint position_buffer, const GLuint index_buffer, const vec3& eye_position);
	void set_transparent_shader_uniforms(context& ctx, view* view_ptr, shader_program& prog);
	void do_final_blend(context& ctx);
	
public:
	class fiber_viewer();
	std::string get_type_name() const { return "fiber_viewer"; }

	void clear(cgv::render::context& ctx);

	bool self_reflect(cgv::reflect::reflection_handler& _rh);
	void stream_help(std::ostream& _os);
	void stream_stats(std::ostream& _os);

	bool handle(cgv::gui::event& _e);
	void on_set(void* member_ptr);

	bool init(cgv::render::context& ctx);
	void init_frame(cgv::render::context& ctx);
	void draw(cgv::render::context& ctx);

	void create_gui();
};

//Alaleh
void rp2ColorMapping(float[3], float[3], float[3]);

void getniidata();
