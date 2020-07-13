#pragma once

#include <cgv/render/context.h>
#include <cgv/render/render_types.h>
#include <cgv/render/shader_program.h>
#include <cgv_gl/gl/gl.h>

using namespace cgv::render;

class gpu_sorter : public render_types {
private:
	unsigned int count;
	unsigned int n_pad;

	unsigned int group_size;
	unsigned int group_count;
	unsigned int scan_group_size;
	
	GLuint distance_in_ssbo;
	GLuint distance_out_ssbo;
	GLuint indices_out_ssbo;
	GLuint prefix_sum_ssbo;
	GLuint blocksums_ssbo;

	/// shader programs
	shader_program distance_prog;
	shader_program scan_local_prog;
	shader_program scan_global_prog;
	shader_program scatter_prog;
	
	bool load_shader_progs(context& ctx);
	void delete_buffers();

public:
	gpu_sorter();
	~gpu_sorter();

	bool init(context& ctx, size_t position_count);
	void sort(context& ctx, GLuint position_buffer, GLuint index_buffer, vec3 eye_position);

	unsigned int get_padding() { return n_pad; }
	unsigned int get_group_size() { return group_size; }
};
