#include <gpu_sorter.h>

gpu_sorter::gpu_sorter() {

	count = 0;
	n_pad = 0;

	distance_in_ssbo = 0;
	distance_out_ssbo = 0;
	indices_out_ssbo = 0;
	prefix_sum_ssbo = 0;
	blocksums_ssbo = 0;
}

gpu_sorter::~gpu_sorter() {

	delete_buffers();
}

bool gpu_sorter::init(context& ctx, size_t position_count) {

	if(!load_shader_progs(ctx))
		return false;

	delete_buffers();

	count = position_count;

	unsigned int n = count;
	group_size = 64;

	unsigned int block_size = 4 * group_size * 2;

	// Calculate padding for n to next multiple of blocksize.
	n_pad = block_size - (n % (block_size));
	if(n % block_size == 0)
		n_pad = 0;

	group_count = (n + n_pad + group_size - 1) / group_size;

	scan_group_size = (n + n_pad + block_size - 1) / block_size;
	unsigned int blocksum_offset_shift = static_cast<unsigned int>(log2f(block_size));
	blocksum_offset_shift -= 1;

	unsigned int n_blocksums = scan_group_size * 2;
	unsigned int num = 1;
	while(n_blocksums > num)
		num <<= 1;
	n_blocksums = num;

	size_t data_size = (n + n_pad) * sizeof(unsigned int);
	size_t blocksums_size = 4 * n_blocksums * sizeof(unsigned int);
	size_t prefix_sum_size = data_size;

	glGenBuffers(1, &distance_in_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, distance_in_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, data_size, (void*)0, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &distance_out_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, distance_out_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, data_size, (void*)0, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &indices_out_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, indices_out_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, data_size, (void*)0, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &prefix_sum_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, prefix_sum_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, prefix_sum_size, (void*)0, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &blocksums_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, blocksums_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, blocksums_size, (void*)0, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	distance_prog.enable(ctx);
	distance_prog.set_uniform(ctx, "n", n);
	distance_prog.set_uniform(ctx, "n_padded", n + n_pad);
	distance_prog.disable(ctx);

	scan_local_prog.enable(ctx);
	scan_local_prog.set_uniform(ctx, "n", n + n_pad);
	scan_local_prog.disable(ctx);

	scan_global_prog.enable(ctx);
	scan_global_prog.set_uniform(ctx, "n", n_blocksums);
	scan_global_prog.disable(ctx);

	scatter_prog.enable(ctx);
	scatter_prog.set_uniform(ctx, "n", n + n_pad);
	scatter_prog.set_uniform(ctx, "blocksum_offset_shift", blocksum_offset_shift);
	scatter_prog.disable(ctx);

	return true;
}

void gpu_sorter::sort(context& ctx, GLuint position_buffer, GLuint index_buffer, vec3 eye_pos) {

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, position_buffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, distance_in_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, index_buffer);

	distance_prog.enable(ctx);
	distance_prog.set_uniform(ctx, "eye_pos", eye_pos);
	glDispatchCompute(group_size, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	distance_prog.disable(ctx);


	// Example for reading an OpenGL buffer into a vector
	/*std::vector<float> data(3, 0);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, distance_in_ssbo);
	void* ptr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, 3, GL_MAP_WRITE_BIT);
	memcpy(data.data(), ptr, 3 * sizeof(float));
	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);*/



	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, prefix_sum_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, blocksums_ssbo);

	for(unsigned int b = 0; b < 32; b += 2) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, distance_in_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, distance_out_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, index_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, indices_out_ssbo);

		scan_local_prog.enable(ctx);
		scan_local_prog.set_uniform(ctx, "bit", b);
		glDispatchCompute(scan_group_size, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scan_local_prog.disable(ctx);

		scan_global_prog.enable(ctx);
		glDispatchCompute(1, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scan_global_prog.disable(ctx);

		scatter_prog.enable(ctx);
		scatter_prog.set_uniform(ctx, "bit", b);
		glDispatchCompute(group_count, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scatter_prog.disable(ctx);

		std::swap(distance_in_ssbo, distance_out_ssbo);
		std::swap(index_buffer, indices_out_ssbo);
	}

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, 0);
}

bool gpu_sorter::load_shader_progs(context& ctx) {

	bool res = true;

	if(!distance_prog.is_created()) {
		if(!distance_prog.build_program(ctx, "distance.glpr", true)) {
			std::cerr << "ERROR in gpu_sorter::init() ... could not build program distance.glpr" << std::endl;
			res = false;
		}
	}

	if(!scan_local_prog.is_created()) {
		if(!scan_local_prog.build_program(ctx, "scan_local.glpr", true)) {
			std::cerr << "ERROR in gpu_sorter::init() ... could not build program scan_local.glpr" << std::endl;
			res = false;
		}
	}

	if(!scan_global_prog.is_created()) {
		if(!scan_global_prog.build_program(ctx, "scan_global.glpr", true)) {
			std::cerr << "ERROR in gpu_sorter::init() ... could not build program scan_global.glpr" << std::endl;
			res = false;
		}
	}

	if(!scatter_prog.is_created()) {
		if(!scatter_prog.build_program(ctx, "scatter.glpr", true)) {
			std::cerr << "ERROR in gpu_sorter::init() ... could not build program scatter.glpr" << std::endl;
			res = false;
		}
	}

	return res;
}

void gpu_sorter::delete_buffers() {

	if(distance_in_ssbo != 0)
		glDeleteBuffers(1, &distance_in_ssbo);

	if(distance_out_ssbo != 0)
		glDeleteBuffers(1, &distance_out_ssbo);

	if(indices_out_ssbo != 0)
		glDeleteBuffers(1, &indices_out_ssbo);

	if(prefix_sum_ssbo != 0)
		glDeleteBuffers(1, &prefix_sum_ssbo);

	if(blocksums_ssbo != 0)
		glDeleteBuffers(1, &blocksums_ssbo);
}
