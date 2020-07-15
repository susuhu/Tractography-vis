#include "fiber_viewer.h"
#include <cgv/math/ftransform.h>
#include <cgv/gui/trigger.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/reflect/reflect_enum.h>
#include <cgv/signal/rebind.h>
#include <cgv_gl/gl/gl.h>
#include <cgv_gl/gl/gl_tools.h>
#include <cgv/media/image/image_reader.h>
#include <cgv/utils/file.h>
#include <cgv/utils/big_binary_file.h>
#include <cgv/utils/advanced_scan.h>
#include<iostream>



namespace cgv {
	namespace reflect {
	}
}

fiber_viewer::fiber_viewer() {

	set_name("Fiber Viewer");

	dataset_filename = "";
	render_mode = RM_DEFERRED;
	color_source = CS_MIDPOINT;

	alss = ALSS_2;
	voxel_resolution = VR_256;

	tstyle.surface_color = rgb(1.0);
	tstyle.illumination_mode = IM_OFF;
	tstyle.material.set_brdf_type(cgv::media::illum::BrdfType::BT_LAMBERTIAN);
	tstyle.ao_offset = 0.004f;
	tstyle.ao_distance = 0.3f;
	tstyle.ao_strength = 1.0f;
	alpha_scale = 1.0f;

	//background_color = rgba(0.0f, 0.0f, 0.15f, 1.0f);
	background_color = rgba(1.0f, 1.0f, 1.0f, 1.0f);
	
	check_for_click = -1.0;

	do_change_dataset = false;
	do_change_color_source = false;
	do_create_density_volume = false;
	do_rebuild_framebuffer = false;
	do_rebuild_buffers = false;

	disable_sorting = false;
	disable_clipping = false;

	view_ptr = nullptr;

	// This is the base path for all resource files. Change this to the folder where you put the .nii files.
	resource_path = "C:\\dev\\mycpp\\volume_data\\";
}

void fiber_viewer::clear(cgv::render::context& ctx) {

	cgv::render::ref_volume_renderer(ctx, -1);

	tr.destruct(ctx);
}

bool fiber_viewer::self_reflect(cgv::reflect::reflection_handler& _rh) {
	return false;
	_rh.reflect_member("dataset_filename", dataset_filename); //&&
	//_rh.reflect_member("member2", member2);
}

void fiber_viewer::stream_help(std::ostream& os) {
	
	os << "fiber_viewer: rendering ... Ambient <O>cclusion\n" << std::endl;
}

void fiber_viewer::stream_stats(std::ostream& os) {
	
	os << "statistics not implemented";
}

bool fiber_viewer::handle(cgv::gui::event& e) {

	unsigned et = e.get_kind();

	if(et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&) e;
		cgv::gui::KeyAction ka = ke.get_action();

		if(ka == cgv::gui::KA_PRESS) {
			switch(ke.get_key()) {
				case 'O':
					tstyle.enable_ambient_occlusion = !tstyle.enable_ambient_occlusion;
					on_set(&tstyle.enable_ambient_occlusion);
					post_redraw();
					break;
			default:
				return false;
			}
		}
	} else if(et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&) e;
		
		switch(me.get_action()) {
		case cgv::gui::MA_PRESS:
			if(me.get_button() == cgv::gui::MouseButton::MB_LEFT_BUTTON && me.get_modifiers() == 0) {
				check_for_click = me.get_time();
				return true;
			}
			break;
		case cgv::gui::MA_RELEASE:
			if(check_for_click > -1.0) {
				double dt = me.get_time() - check_for_click;
				if(dt < 0.2 && render_mode == RM_DEFERRED) {
					if(get_context() && view_ptr) {
						cgv::render::context& ctx = *get_context();
						dvec3 p;

						fb.fb.enable(ctx);
						double z = view_ptr->get_z_and_unproject(ctx, me.get_x(), me.get_y(), p);
						fb.fb.disable(ctx);

						if(z > 0.0 && z < 1.0) {
							view_ptr->set_focus(p);

							post_redraw();
							return true;
						}
					}
				}
				check_for_click = -1.0;
			}
			break;
		case cgv::gui::MA_DRAG:
			check_for_click = -1.0;
			return false;
			break;
		default:
			return false;
		}
	} else {
		return false;
	}

	return true;
}

void fiber_viewer::on_set(void* member_ptr) {

	if(member_ptr == &dataset_filename) {
		do_change_dataset = true;
	}

	if(member_ptr == &color_source) {
		do_change_color_source = true;
	}

	if(member_ptr == &voxel_resolution || member_ptr == &render_mode) {
		do_create_density_volume = true;
	}

	if(member_ptr == &fb.cf) {
		cb.cf = fb.cf;
		do_rebuild_framebuffer = true;
	}

	if(member_ptr == &alss) {
		do_rebuild_buffers = true;
	}

	if(member_ptr == &tstyle.enable_ambient_occlusion) {
		std::string defines = "ENABLE_AMBIENT_OCCLUSION=";
		defines += std::to_string((int)tstyle.enable_ambient_occlusion);
		
		context* ctx_ptr = get_context();

		if(ctx_ptr) {
			context& ctx = *ctx_ptr;
			load_shader(ctx, tube_transparent_naive_prog, "tube_transparent_naive", defines);
			//load_shader(ctx, tube_transparent_al_prog, "tube_transparent_al", defines);
		}
	}

	update_member(member_ptr);
	post_redraw();
}

bool fiber_viewer::generate_test_dataset() {

	std::mt19937 rng(42);
	std::uniform_real_distribution<float> distr(-1.0f, 1.0f);

	vec3 pos_a(-0.5f, 0.6f, 0.0f);
	vec3 pos_b(0.5f, 0.6f, -0.5f);

	// First tract from pos_a to pos_b
	raw_positions.push_back(pos_a);
	raw_positions.push_back(pos_b);

	raw_radii.push_back(0.05f);
	raw_radii.push_back(0.1f);

	tracts.push_back(tract{ 0u, 2u }); // first tract starts at offset 0 and has 2 entires

	vec3 start(0.0f);

	// Second tract with 5 points
	for(unsigned i = 0; i < 5; ++i) {
		raw_positions.push_back(start);
		raw_radii.push_back(0.05f);

		start[0] += 0.5f;
		start[1] += 0.3f * distr(rng);
		start[2] += 0.3f * distr(rng);
	}

	tracts.push_back(tract{ 2u, 5u }); // second tract starts at offset 2 and has 5 entries

	return true;
}

/*
	Loads fiber tractography files of format .trk as specified at http://www.trackvis.org/docs/?subsect=fileformat.
*/
bool fiber_viewer::read_trk_file(std::string file) {

	cgv::utils::big_binary_file f;

	if(f.open(cgv::utils::big_binary_file::READ, file)) {
		char buffer[6];

		f.read(buffer);
		std::string magic_id(buffer);

		if(magic_id != "TRACK") {
			std::cout << "Error: could not read " << file << "!" << std::endl;
			return false;
		}

		short int dim_si[3];
		ivec3 dim;
		vec3 voxel_size, origin;
		f.read(dim_si);
		f.read(voxel_size);
		f.read(origin);

		dim = ivec3(dim_si[0], dim_si[1], dim_si[2]);

		short int n_scalars;
		f.read(n_scalars);

		std::vector<std::string> scalar_names(10);
		for(int i = 0; i < 10; ++i) {
			char scalar_name[20];
			f.read(scalar_name);
			scalar_names[i] = std::string(scalar_name);
		}

		short int n_properties;
		f.read(n_properties);

		std::vector<std::string> property_names(10);
		for(int i = 0; i < 10; ++i) {
			char property_name[20];
			f.read(property_name);
			property_names[i] = std::string(property_name);
		}

		mat4 vox_to_ras(1.0f);
		f.read(vox_to_ras);
		bool has_voxel_to_ras_transformation = vox_to_ras(3, 3) != 0.0f;

		if(!has_voxel_to_ras_transformation)
			vox_to_ras.identity();

		// Multiply a matrix that transforms into opengl space (e.g flip y and z and invert x)
		// Also scale the dataset down to prevent numerical instabilities resulting in ambient occlusion artifacts
		mat4 flip(0.0f);
		flip(0, 0) = -0.1f;
		flip(1, 2) = 0.1f;
		flip(2, 1) = 0.1f;
		flip(3, 3) = 1.0f;
		vox_to_ras = flip * vox_to_ras;

		// skip reserved space
		long long p = f.position();
		f.seek(p + 444ll);

		char voxel_order[4];
		f.read(voxel_order);

		// skip pad2, image_orientation_patient and pad1
		p = f.position();
		f.seek(p + 4ll + 24ll + 2ll);

		// skip internal invert and swap flags
		p = f.position();
		f.seek(p + 6ll);

		int32_t n_count;
		f.read(n_count);
		bool number_of_tracks_stored = n_count != 0;

		// skip version number
		p = f.position();
		f.seek(p + 4ll);

		int32_t hdr_size;
		f.read(hdr_size);

		if(hdr_size != 1000) {
			std::cout << "Error: could not read " << file << "!" << std::endl;
			return false;
		}

		long long bytes_left = f.size() - f.position();

		// Now read the acual track data
		int count = 0;
		while(bytes_left > 0) {
			int32_t track_count; // number of points in this track
			f.read(track_count);

			// read all track points
			for(int32_t i = 0; i < track_count; ++i) {
				// read position
				vec3 pos;
				f.read(pos);

				vec4 pos4 = vox_to_ras * vec4(pos[0], pos[1], pos[2], 1.0f);
				pos[0] = pos4[0];
				pos[1] = pos4[1];
				pos[2] = pos4[2];

				raw_positions.push_back(pos);

				// read scalars (skip for now)
				p = f.position();
				f.seek(p + (long long)n_scalars * 4ll);
			}

			// read properties (skip for now)
			p = f.position();
			f.seek(p + (long long)n_properties * 4ll);

			tracts.push_back(tract{ (unsigned)count, (unsigned)track_count });
			count += track_count;
			bytes_left = f.size() - f.position();
		}
	}
}

void fiber_viewer::set_dataset(context& ctx, bool generate_test) {

	// Clear and delete old buffers if present
	if(segment_ibo > 0) {
		glDeleteBuffers(1, &segment_ibo);
		segment_ibo = 0;
	}

	if(ibo > 0) {
		glDeleteBuffers(1, &ibo);
		ibo = 0;
	}

	if(positions_ssbo > 0) {
		glDeleteBuffers(1, &positions_ssbo);
		positions_ssbo = 0;
	}

	if(radii_ssbo > 0) {
		glDeleteBuffers(1, &radii_ssbo);
		radii_ssbo = 0;
	}

	if(colors_ssbo > 0) {
		glDeleteBuffers(1, &colors_ssbo);
		colors_ssbo = 0;
	}


	tracts.clear();
	raw_positions.clear();
	raw_radii.clear();
	raw_attributes.clear();

	positions.clear();
	radii.clear();
	colors_midpoint.clear();
	colors_segment.clear();
	colors_attribute.clear();
	colors_coolwarm.clear();
	colors_extended_kindlmann.clear();
	colors_extended_blackbody.clear();
	colors_blackbody.clear();
	colors_isorainbow.clear();
	colors_boys.clear();
	
	// Clear the renderer
	tr.destruct(ctx);

	if(!tr.init(ctx))
		return;

	// Generate or load a dataset
	std::cout << "=====\nGenerating/loading data... ";
	util::timer t;

	tstyle.radius_scale = 1.0f;

	bool success = false;

	if(generate_test) {
		tstyle.radius = 0.02f;
		success = generate_test_dataset();
	} else {
		tstyle.radius = 0.015f;
		// Read dataset from file
		success = read_trk_file(dataset_filename);
	}

	if(!success) {
		// TODO: clear data
		return;
	}

	dataset_bbox = box3(1.0f, -1.0f);
	for(unsigned i = 0; i < raw_positions.size(); ++i) {
		vec3 p = raw_positions[i];

		dataset_bbox.add_point(p - tstyle.radius);
		dataset_bbox.add_point(p + tstyle.radius);
	}

	dataset_bbox.add_point(dataset_bbox.get_min_pnt() - 0.01f);
	dataset_bbox.add_point(dataset_bbox.get_max_pnt() + 0.01f);

	// Move dataset to positive octant of the world
	// We then use the sign bit of the x-component of the position to encode if the tube shall be clipped at this position.
	// Clipping removes internal overlapping structures of transparent tubes.
	vec3 offset = -dataset_bbox.get_min_pnt();

	for(size_t i = 0; i < raw_positions.size(); ++i)
		raw_positions[i] += offset;

	// Update the bounding box according to the new position
	dataset_bbox.ref_min_pnt() = vec3(0.0f);
	dataset_bbox.ref_max_pnt() += offset;

	dataset_center = dataset_bbox.get_center();

	// Move the camera to show the dataset cenetred in the viewport
	view_ptr->set_focus(dataset_bbox.get_center());
	view_ptr->set_y_extent_at_focus((double)length(dataset_bbox.get_extent()) * 0.6);

	t.stop();
	std::cout << "done in " << t.seconds() << "s\n=====" << std::endl;

	// Create tube positions and colors from generated data
	std::cout << "=====\nPreparing data... ";
	t.restart();

	prepare_data(ctx);

	t.stop();
	std::cout << "done in " << t.seconds() << "s" << std::endl;
	std::cout << "Number of tracts: " << tracts.size() << std::endl;
	std::cout << "Number of segments: " << (positions.size() / 2) << "\n=====" << std::endl;

	// Generate the density volume used for ambient occlusion
	create_density_volume(ctx, dataset_bbox, tstyle.radius * tstyle.radius_scale);

	// Set data in the renderer
	tr.set_render_style(tstyle);
	tr.set_position_array(ctx, positions);
	if(radii.size() == positions.size())
		tr.set_radius_array(ctx, radii);
	set_color_source(ctx);

	// Set up gpu sorter and index buffers used for sorting the tube segments and indexed rendering
	unsigned segment_count = positions.size() / 2;

	if(!sorter.init(ctx, segment_count))
		return;

	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, positions.size() * sizeof(unsigned), (void*)0, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glGenBuffers(1, &segment_ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, segment_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, segment_count * sizeof(unsigned), (void*)0, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/*
	Prepares the data for rendering. All consecutive tracts (lines) need to be rendered on a per-segment basis.
	Converts points in a line from:
		1 -> 2 -> 3 -> 4
	to:
		1,2 2,3 3,4

	by duplicating position, radius and color where two segments of a connected line meet.
*/
void fiber_viewer::prepare_data(context& ctx) {

	std::vector<vec3> tposs;
	
	for(unsigned i = 0; i < tracts.size(); ++i) {
		int o = tracts[i].offset;
		int s = tracts[i].size;

		int mid = o + s / 2;
		if(s % 2 == 0)
			mid -= 1;
		rgb color(0.0f);

		vec3 dir(0.0f);
		rgba last_color(0.0f);

		for(unsigned j = o; j < o + s - 1; ++j) {
			vec3 a = raw_positions[j];
			vec3 b = raw_positions[j + 1];

			if(j == mid) {
				vec3 dir = normalize(a - b);
				dir.abs();
				color = rgb(dir[0], dir[2], dir[1]);
			}

			if(j == o) {
				vec3 last_dir = normalize(b - a);
				last_dir.abs();
				last_color = rgba(last_dir[0], last_dir[2], last_dir[1], 1.0f);
			}

			if(j == o + s - 2) {
				dir = normalize(b - a);
			} else {
				dir = normalize(raw_positions[j + 2] - a);
			}

			dir.abs();
			colors_segment.push_back(last_color);
			last_color = rgba(dir[0], dir[2], dir[1], 1.0f);
			colors_segment.push_back(last_color);

			positions.push_back(a);
			positions.push_back(b);

			if(j > o)
				a[0] *= -1.0f;

			if(j < o + s - 2)
				b[0] *= -1.0f;

			tposs.push_back(a);
			tposs.push_back(b);
		}

		for(unsigned j = o; j < o + s - 1; ++j) {
			rgba col4 = rgba(color.R(), color.G(), color.B(), 1.0f);
			colors_midpoint.push_back(col4);
			colors_midpoint.push_back(col4);
		}
	}

	//Scalar Colormapping

	//read .niidata
	//read fa data
	nifti_image* nii1 = nifti_image_read((resource_path + "dti_FA.nii").c_str(), 1);
	if (!nii1) {
		fprintf(stderr, "** failed to read NIfTI from '%s'.\n", resource_path + "dti_FA.nii");
		//return 2;
	}
	
	//read md data
	nifti_image* nii2 = nifti_image_read((resource_path + "dti_MD.nii").c_str(), 1);
	float* ptrdata_md = (float*)nii2->data;
	
	// Get dimensions of input
	int size_x = nii1->nx;
	int size_y = nii1->ny;
	int size_z = nii1->nz;
	int size_time = nii1->nt;
	int nx = nii1->nx;
	int nxy = nii1->nx * nii1->ny;
	int nxyz = nii1->nx * nii1->ny * nii1->nz;
	int nr_voxels = size_time * size_z * size_y * size_x;
	float* ptrdata = (float*)nii1->data;

	//check fa data
	float fa_max = 0;
	float fa_avr = 0;
	for (int i = 0; i < nr_voxels; i++) {
		fa_avr = fa_avr + ptrdata[i];
		if (ptrdata[i] > fa_max) {
			fa_max = ptrdata[i];
		}
	}
	fa_avr = fa_avr / nr_voxels;
	
	//check md data
	float md_min = 1;
	float md_max = 0;
	float md_scale;
	for (int i = 0; i < nr_voxels; i++) {
		if (ptrdata_md[i] > md_max) {
			md_max = ptrdata_md[i];
		}
		if (ptrdata_md[i] < md_min) {
			md_min = ptrdata_md[i];
		}
	}
	md_scale = md_max - md_min;

	std::cout << "=============AVR=FA===============: " << fa_avr << std::endl;
	std::cout << "=============MAX=FA===============: " << fa_max << std::endl;


	// The FA and the other .nii volumes define their up axis as z. This does not
	// coincide with the OpenGL standard, where the y axis points upwards. To remedy
	// this we need to flip y and z in the volume data. While flipping we read the
	// data from the nifti_image object and write it into a vector that we use to
	// fill a texture (fa_tex) for alter display.
	fa_tex.texture.clear();
	fa_tex.data.resize(nr_voxels);

	for(unsigned i = 0; i < nr_voxels; ++i) {
		uvec3 coord = util::idx2coord(i, ivec3(size_x, size_z, size_y));
		std::swap(coord[1], coord[2]);
		unsigned idx = util::coord2idx(coord, ivec3(size_x, size_y, size_z));

		// This creates an outer shell around the volume data to visualize the boundaries.
		// You can remove this once you have finished trimming off the empty regions.
		// TODO: remove
		if(	coord[0] == 0 || coord[0] == size_x - 1 ||
			coord[1] == 0 || coord[1] == size_y - 1 ||
			coord[2] == 0 || coord[2] == size_z - 1)
			fa_tex.data[i] = 0.2f;
		else
			fa_tex.data[i] = ptrdata[idx];
		// end remove

		// TODO: enable
		//fa_tex.data[i] = ptrdata[idx];
	}

	// We flipped the values so now we also need to flip the size in y and z direction
	std::swap(size_y, size_z);



	// TODO: Trim the empty volume space.
	// Open the program and load the brain.trk file. Tahke a look at the shape of the brain
	// and switch the render mdoe to "volume". You should see a rendering of the loaded fa_dti
	// volume data with a highlighted shell, which shows the bounding box. Notice how the brain
	// shapes of the tubes and this volume are different. There is lots of empty space around
	// the brain which needs to be removed in order to align the volume data with the tract geometry
	// as good as possible. Implement an algorithm which removes this empty space on all coordinate
	// axes and then remove the code for creating the boundig box shell in the prevoius loop. You
	// can use a temporary vector to hold the new data and write it back to the texture data once
	// finished. Make sure to set the size to the new parameters.

	// !implement here!



	// Set the volume transformation parameters to match the bounding box
	mat4 vol_transformation = cgv::math::translate4(dataset_bbox.get_min_pnt()) * cgv::math::scale4(dataset_bbox.get_extent());
	vstyle.transformation_matrix = vol_transformation;

	// Set some texture informations
	fa_tex.connect(new cgv::data::data_format(size_x, size_y, size_z, cgv::type::info::TypeId::TI_FLT32, cgv::data::ComponentFormat::CF_R));
	fa_tex.texture.create(ctx, fa_tex.view, 0);
	fa_tex.texture.set_min_filter(TF_LINEAR_MIPMAP_LINEAR);
	fa_tex.texture.set_mag_filter(TF_LINEAR);
	fa_tex.texture.set_wrap_s(TW_CLAMP_TO_BORDER);
	fa_tex.texture.set_wrap_t(TW_CLAMP_TO_BORDER);
	fa_tex.texture.set_wrap_r(TW_CLAMP_TO_BORDER);
	fa_tex.texture.set_border_color(0.0f, 0.0f, 0.0f, 0.0f);
	fa_tex.texture.generate_mipmaps(ctx);







	rgba col5[8];
	col5[0] = rgba(0.3f, 0.3f, 0.8f, 1.0f);
	col5[1] = rgba(0.5f, 0.5f, 0.9f, 1.0f);
	col5[2] = rgba(0.6f, 0.7f, 1.0f, 1.0f);
	col5[3] = rgba(0.8f, 0.8f, 0.9f, 1.0f);
	col5[4] = rgba(0.9f, 0.8f, 0.8f, 1.0f);
	col5[5] = rgba(0.9f, 0.7f, 0.5f, 1.0f);
	col5[6] = rgba(0.9f, 0.4f, 0.3f, 1.0f);
	col5[7] = rgba(0.7f, 0.0f, 0.2f, 1.0f);

	

	util::linear_interpolator coolwarm_colormap;
	coolwarm_colormap.values.clear();
	coolwarm_colormap.values.push_back(col5[0]);
	coolwarm_colormap.values.push_back(col5[1]);
	coolwarm_colormap.values.push_back(col5[2]);
	coolwarm_colormap.values.push_back(col5[3]);
	coolwarm_colormap.values.push_back(col5[4]);
	coolwarm_colormap.values.push_back(col5[5]);
	coolwarm_colormap.values.push_back(col5[6]);
	coolwarm_colormap.values.push_back(col5[7]);


	rgba col6[8];
	col6[0] = rgba(0.0f, 0.0f, 0.0f, 1.0f);
	col6[1] = rgba(0.2f, 0.0f, 0.5f, 1.0f);
	col6[2] = rgba(0.0f, 0.3f, 0.2f, 1.0f);
	col6[3] = rgba(0.2f, 0.5f, 0.0f, 1.0f);
	col6[4] = rgba(0.9f, 0.4f, 0.0f, 1.0f);
	col6[5] = rgba(1.0f, 0.5f, 0.8f, 1.0f);
	col6[6] = rgba(0.9f, 0.8f, 1.0f, 1.0f);
	col6[7] = rgba(1.0f, 1.0f, 1.0f, 1.0f);


	util::linear_interpolator extended_kindlmann_colormap;
	extended_kindlmann_colormap.values.clear();
	extended_kindlmann_colormap.values.push_back(col6[0]);
	extended_kindlmann_colormap.values.push_back(col6[1]);
	extended_kindlmann_colormap.values.push_back(col6[2]);
	extended_kindlmann_colormap.values.push_back(col6[3]);
	extended_kindlmann_colormap.values.push_back(col6[4]);
	extended_kindlmann_colormap.values.push_back(col6[5]);
	extended_kindlmann_colormap.values.push_back(col6[6]);
	extended_kindlmann_colormap.values.push_back(col6[7]);

	rgba coleb[8];
	coleb[0] = rgba(0.0f, 0.0f, 0.0f, 1.0f);
	coleb[1] = rgba(0.2f, 0.0f, 0.3f, 1.0f);
	coleb[2] = rgba(0.4f, 0.1f, 0.4f, 1.0f);
	coleb[3] = rgba(0.6f, 0.2f, 0.4f, 1.0f);
	coleb[4] = rgba(0.8f, 0.3f, 0.3f, 1.0f);
	coleb[5] = rgba(0.9f, 0.5f, 0.1f, 1.0f);
	coleb[6] = rgba(0.9f, 0.8f, 0.1f, 1.0f);
	coleb[7] = rgba(1.0f, 1.0f, 0.6f, 1.0f);

	util::linear_interpolator extended_blackbody_colormap;
	extended_blackbody_colormap.values.clear();
	extended_blackbody_colormap.values.push_back(coleb[0]);
	extended_blackbody_colormap.values.push_back(coleb[1]);
	extended_blackbody_colormap.values.push_back(coleb[2]);
	extended_blackbody_colormap.values.push_back(coleb[3]);
	extended_blackbody_colormap.values.push_back(coleb[4]);
	extended_blackbody_colormap.values.push_back(coleb[5]);
	extended_blackbody_colormap.values.push_back(coleb[6]);
	extended_blackbody_colormap.values.push_back(coleb[7]);

	rgba colbb[8];
	colbb[0] = rgba(0.0f, 0.0f, 0.0f, 1.0f);
	colbb[1] = rgba(0.2f, 0.1f, 0.0f, 1.0f);
	colbb[2] = rgba(0.5f, 0.1f, 0.1f, 1.0f);
	colbb[3] = rgba(0.7f, 0.2f, 0.1f, 1.0f);
	colbb[4] = rgba(0.9f, 0.4f, 0.0f, 1.0f);
	colbb[5] = rgba(0.9f, 0.6f, 0.0f, 1.0f);
	colbb[6] = rgba(0.9f, 0.8f, 0.2f, 1.0f);
	colbb[7] = rgba(1.0f, 1.0f, 1.0f, 1.0f);


	util::linear_interpolator blackbody_colormap;
	blackbody_colormap.values.clear();
	blackbody_colormap.values.push_back(colbb[0]);
	blackbody_colormap.values.push_back(colbb[1]);
	blackbody_colormap.values.push_back(colbb[2]);
	blackbody_colormap.values.push_back(colbb[3]);
	blackbody_colormap.values.push_back(colbb[4]);
	blackbody_colormap.values.push_back(colbb[5]);
	blackbody_colormap.values.push_back(colbb[6]);
	blackbody_colormap.values.push_back(colbb[7]);
	

	rgba colrb[7];
	colrb[0] = rgba(1.0f, 0.0f, 0.0f, 1.0f);
	colrb[1] = rgba(1.0f, 0.5f, 0.0f, 1.0f);
	colrb[2] = rgba(1.0f, 1.0f, 1.0f, 1.0f);
	colrb[3] = rgba(0.0f, 1.0f, 0.0f, 1.0f);
	colrb[4] = rgba(0.0f, 0.0f, 1.0f, 1.0f);
	colrb[5] = rgba(0.2f, 0.2f, 0.4f, 1.0f);
	colrb[6] = rgba(0.5f, 0.0f, 1.0f, 1.0f);

	util::linear_interpolator isorainbow_colormap;
	isorainbow_colormap.values.clear();
	isorainbow_colormap.values.push_back(colrb[0]);
	isorainbow_colormap.values.push_back(colrb[1]);
	isorainbow_colormap.values.push_back(colrb[2]);
	isorainbow_colormap.values.push_back(colrb[3]);
	isorainbow_colormap.values.push_back(colrb[4]);	
	isorainbow_colormap.values.push_back(colrb[5]);
	isorainbow_colormap.values.push_back(colrb[6]);


	for (unsigned i = 0; i < tracts.size(); ++i) {
		int o = tracts[i].offset;
		int s = tracts[i].size;		

		for (unsigned j = o; j < o + s - 1; ++j) {
			vec3 a = raw_positions[j];
			vec3 b = raw_positions[j + 1];
			float fa;
			float md;

			//ivec3 fa_index_a = ivec3(int(raw_positions[j].x() * (116 / 26.9)), int(raw_positions[j].y() * (116 / 27.9)), int(raw_positions[j].z() * (80 / 32.2)));
			//ivec3 fa_index_b = ivec3(int(raw_positions[j+1].x() * (116 / 26.9)), int(raw_positions[j+1].y() * (116 / 27.9)), int(raw_positions[j+1].z() * (80 / 32.2)));
			ivec3 fa_index_a = ivec3(int(raw_positions[j].x() * (116 / dataset_bbox.ref_max_pnt().x())), int(raw_positions[j].z() * (116 / dataset_bbox.ref_max_pnt().z())), int(raw_positions[j].y() * (80 / dataset_bbox.ref_max_pnt().y())));
			ivec3 fa_index_b = ivec3(int(raw_positions[j+1].x() * (116 / dataset_bbox.ref_max_pnt().x())), int(raw_positions[j+1].z() * (116 / dataset_bbox.ref_max_pnt().z())), int(raw_positions[j+1].y() * (80 / dataset_bbox.ref_max_pnt().y())));
			int m = fa_index_a.x() + fa_index_a.y() * 116 + fa_index_a.z()*116*116;
			int n = fa_index_b.x() + fa_index_b.y() * 116 + fa_index_b.z()*116*116;
			fa = (ptrdata[m] + ptrdata[n]) / 2;
			md = (ptrdata_md[m] + ptrdata_md[n]-2* md_min)/md_scale/2;
			float alphamd = md*100;//md is very small, and there is negative value. almost -0.001 to 0.003.
			
			//if (alpha > 1) {
			//	alpha = 1;
			//}

			//if (j <= nii1->nvox) {
				//fa = (ptrdata[j + 1] + ptrdata[j]) / 2;

				float alpha = (float)(fa / fa_max); //if you want to change to md image: float alpha = alphamd;

				//rgba color = coolwarm_colormap.interpolate(alpha);
				rgba color_a = coolwarm_colormap.interpolate(ptrdata[m]);
				rgba color_b = coolwarm_colormap.interpolate(ptrdata[n]);
				colors_coolwarm.push_back(color_a);
				colors_coolwarm.push_back(color_b);

				//rgba color2 = extended_kindlmann_colormap.interpolate(alpha);
				rgba color_a2 = extended_kindlmann_colormap.interpolate(ptrdata[m]);
				rgba color_b2 = extended_kindlmann_colormap.interpolate(ptrdata[n]);
				colors_extended_kindlmann.push_back(color_a2);
				colors_extended_kindlmann.push_back(color_b2);

				//rgba color3 = extended_blackbody_colormap.interpolate(alpha);
				rgba color_a3 = extended_blackbody_colormap.interpolate(ptrdata[m]);
				rgba color_b3 = extended_blackbody_colormap.interpolate(ptrdata[n]);
				colors_extended_blackbody.push_back(color_a3);
				colors_extended_blackbody.push_back(color_b3);

				//rgba color4 = blackbody_colormap.interpolate(alpha);
				rgba color_a4 = blackbody_colormap.interpolate(ptrdata[m]);
				rgba color_b4 = blackbody_colormap.interpolate(ptrdata[n]);
				colors_blackbody.push_back(color_a4);
				colors_blackbody.push_back(color_b4);

				//rgba color5 = isorainbow_colormap.interpolate(alpha);
				rgba color_a5 = isorainbow_colormap.interpolate(ptrdata[m]);
				rgba color_b5 = isorainbow_colormap.interpolate(ptrdata[n]);
				colors_isorainbow.push_back(color_a5);
				colors_isorainbow.push_back(color_b5);
			//}
			
		}
	}
	//Scalar Colormapping

	//Alaleh's boy's surface
	for (unsigned i = 0; i < tracts.size(); ++i) {
		int o = tracts[i].offset;
		int s = tracts[i].size;

		for (unsigned j = o; j < o + s - 1; ++j) {

			vec3 a = raw_positions[j];
			vec3 b = raw_positions[j + 1];
			float rgb_array[3];
			float startPoint[3];
			float endPoint[3];
			for (int i = 0; i < 3; i++)
			{
				startPoint[i] = a[i];
				endPoint[i] = b[i];
			}

			//normalization is done inside the function 
			rp2ColorMapping(startPoint, endPoint, rgb_array);

			rgba col4 = rgba(rgb_array[0], rgb_array[1], rgb_array[2], 1.0f);
			colors_boys.push_back(col4);
			colors_boys.push_back(col4);

		}
	}	//Alaleh's boy's surface


	if(raw_radii.size() == raw_positions.size()) {
		for(unsigned i = 0; i < tracts.size(); ++i) {
			int o = tracts[i].offset;
			int s = tracts[i].size;

			float last_radius = 0.0f;

			for(unsigned j = o; j < o + s - 1; ++j) {
				if(j == o)
					last_radius = raw_radii[j];

				radii.push_back(last_radius);

				last_radius = raw_radii[j + 1];
				radii.push_back(last_radius);
			}
		}
	}

	if(raw_attributes.size() == raw_positions.size()) {
		for(unsigned i = 0; i < tracts.size(); ++i) {
			int o = tracts[i].offset;
			int s = tracts[i].size;

			rgba last_color(0.0f);

			for(unsigned j = o; j < o + s - 1; ++j) {
				if(j == o) {
					float attr = raw_attributes[j];
					attr = cgv::math::clamp(attr, 0.0f, 1.0f);
					last_color = color_map.interpolate(attr);
				}

				colors_attribute.push_back(last_color);

				float attr = raw_attributes[j + 1];
				attr = cgv::math::clamp(attr, 0.0f, 1.0f);
				last_color = color_map.interpolate(attr);

				colors_attribute.push_back(last_color);
			}
		}
	}

	// Create a shader storage buffer object to hold the data for the transparent tubes.
	// We dont use vertex buffer objects here because we need access to the neighbouring
	// segments during rendering.
	glGenBuffers(1, &positions_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, positions_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, tposs.size() * sizeof(vec3), (void*)tposs.data(), GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &radii_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, radii_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, radii.size() * sizeof(float), (void*)radii.data(), GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &colors_ssbo);
}

/*
	Traverses a line through a uniform 3D grid. returns a list containing pairs of grid cell index and intersection length.
*/
std::vector<std::pair<int, float>> traverse_line(vec3& a, vec3& b, vec3& vbox_min, float vsize, ivec3& res) {

	std::vector<std::pair<int, float>> intervals;

	// Amanatides Woo line traversal algorithm
	vec3 dir = normalize(vec3(b - a));
	vec3 dt;
	ivec3 step;
	vec3 orig_grid = a - vbox_min;
	vec3 dest_grid = b - vbox_min;
	vec3 t(0.0f);
	float ct = 0.0f;

	for(unsigned i = 0; i < 3; ++i) {
		float delta = vsize / dir[i];
		if(dir[i] < 0.0f) {
			dt[i] = -delta;
			t[i] = (floor(orig_grid[i] / vsize) * vsize - orig_grid[i]) / dir[i];
			step[i] = -1;
		} else {
			dt[i] = delta;
			t[i] = ((floor(orig_grid[i] / vsize) + 1) * vsize - orig_grid[i]) / dir[i];
			step[i] = 1;
		}
	}

	ivec3 cell_idx(
		(int)(floor(orig_grid.x() / vsize)),
		(int)(floor(orig_grid.y() / vsize)),
		(int)(floor(orig_grid.z() / vsize))
	);

	ivec3 end_idx(
		(int)(floor(dest_grid.x() / vsize)),
		(int)(floor(dest_grid.y() / vsize)),
		(int)(floor(dest_grid.z() / vsize))
	);

	intervals.push_back(std::make_pair<int, float>(cell_idx[0] + res[0] * cell_idx[1] + res[0] * res[1] * cell_idx[2], 0.0f));

	vec3 p = orig_grid;
	size_t idx = 0;

	while(cell_idx != end_idx) {
		if(t[0] < t[1]) {
			if(t[0] < t[2]) {
				cell_idx[0] += step[0];
				if(cell_idx[0] < 0 || cell_idx[0] > res[0] - 1)
					break;
				p = orig_grid + t[0] * dir;
				t[0] += dt[0];
			} else {
				cell_idx[2] += step[2];
				if(cell_idx[2] < 0 || cell_idx[2] > res[2] - 1)
					break;
				p = orig_grid + t[2] * dir;
				t[2] += dt[2];
			}
		} else {
			if(t[1] < t[2]) {
				cell_idx[1] += step[1];
				if(cell_idx[1] < 0 || cell_idx[1] > res[1] - 1)
					break;
				p = orig_grid + t[1] * dir;
				t[1] += dt[1];
			} else {
				cell_idx[2] += step[2];
				if(cell_idx[2] < 0 || cell_idx[2] > res[2] - 1)
					break;
				p = orig_grid + t[2] * dir;
				t[2] += dt[2];
			}
		}

		float l = (orig_grid - p).length() - ct;
		ct += l;
		intervals[idx].second = l;

		intervals.push_back(std::make_pair<int, float>(cell_idx[0] + res[0] * cell_idx[1] + res[0] * res[1] * cell_idx[2], 0.0f));
		++idx;
	}

	float l = (p - dest_grid).length();
	intervals[idx].second = l;

	return intervals;
}

/*
	Rasterizes the line data into a uniform grid by accumulating the density in each grid cell.
	This density can be used to determine how much light passes through each voxel which is used
	to determine the ambient occlusion term.

	Warning: Each time the tube radius or alpha scale (only in transparent rendering modes) changes
	the volume needs to be generated again! This can be done by selecting the same (or a different)
	voxel resolution in the gui.
*/
void fiber_viewer::create_density_volume(const context& ctx, const box3 bbox, const float radius) {

	std::cout << "=====\nGenerating density volume... ";
	util::timer t;

	density_tex.texture.clear();

	vec3 ext = bbox.get_extent();

	unsigned resolution = 8u;
	switch(voxel_resolution) {
	case VR_8: resolution = 8u; break;
	case VR_16: resolution = 16u; break;
	case VR_32: resolution = 32u; break;
	case VR_64: resolution = 64u; break;
	case VR_128: resolution = 128u; break;
	case VR_256: resolution = 256u; break;
	case VR_512: resolution = 512u; break;
	}

	// Calculate the cube voxel size and the resolution in each dimension
	int max_ext_axis = cgv::math::max_index(ext);
	float max_ext = ext[max_ext_axis];
	float vsize = max_ext / static_cast<float>(resolution);

	float vvol = vsize * vsize*vsize; // Volume per voxel

	// Calculate the number of voxels in each dimension
	unsigned resx = static_cast<unsigned>(ceilf(ext.x() / vsize));
	unsigned resy = static_cast<unsigned>(ceilf(ext.y() / vsize));
	unsigned resz = static_cast<unsigned>(ceilf(ext.z() / vsize));

	std::cout << "voxel resolution:" << resx << ", " << resy << ", " << resz << std::endl;

	vec3 vres = vec3(resx, resy, resz);
	vec3 vbox_ext = vsize * vres;
	vec3 vbox_min = bbox.get_min_pnt() - 0.5f*(vbox_ext - ext);

	std::vector<float> voxels(resx*resy*resz, 0.0f);

	bool has_radii = raw_radii.size() == raw_positions.size();
	bool has_attributes = raw_attributes.size() == raw_positions.size();

	// When rendering opaque the transparency has no influence on the density.
	// Transparent tubes however affect the density of the voxels to simulate
	// the effect of blocking less light, when tubes are more transparent.
	float opacity_influence = render_mode == RM_DEFERRED ? 0.0f : 1.0f;

	// Loop over all tracts
	for(unsigned i = 0; i < tracts.size(); ++i) {
		unsigned offset = tracts[i].offset;
		unsigned size = tracts[i].size;

		unsigned from = offset;
		unsigned to = offset + size - 1;

		// Loop over all segments this tract is composed of
		for(unsigned j = from; j < to; ++j) {
			// The start and end points of this segment
			vec3 p0 = raw_positions[j];
			vec3 p1 = raw_positions[j + 1];

			// Get radius and opacity values for the start and end point
			float r0 = radius;
			float r1 = radius;

			float a0 = 1.0f;
			float a1 = 1.0f;

			if(has_radii) {
				r0 = radii[j];
				r1 = radii[j + 1];
			}

			if(has_attributes) {
				a0 = raw_attributes[j];
				a1 = raw_attributes[j + 1];
			}

			// Get the all intervals of cell-segment intersections
			std::vector<std::pair<int, float>> intervals = traverse_line(p0, p1, vbox_min, vsize, ivec3(vres));

			float total_length = (p1 - p0).length();
			float accum_length = 0.0f;

			// Loop over all intervals to calculate the density contribution of this segment for the intersected cells
			for(size_t k = 0; k < intervals.size(); ++k) {
				float length = intervals[k].second;

				// Interpolate radius and opacity over the segment in the current interval
				float alpha0 = accum_length / total_length;
				float alpha1 = (accum_length + length) / total_length;

				float radius0 = (1.0f - alpha0) * r0 + alpha0 * r1;
				float radius1 = (1.0f - alpha1) * r0 + alpha1 * r1;

				float alpha_mid = 0.5f*(alpha0 + alpha1);
				float opacity_scale_factor = (1.0f - alpha_mid) * a0 + alpha_mid * a1;
				// Scale opacity by global opacity scale factor
				opacity_scale_factor *= alpha_scale;

				// density contribution is volume of the segments truncated cone divided by the voxel cell volume
				float vol = (PI / 3.0f) * (r0*r0 + r0 * r1 + r1 * r1) * length;
				float vol_rel = vol / vvol;

				// Reduce volume influence according to opacity of segment
				vol_rel *= 1.0f - opacity_influence * (1.0f - opacity_scale_factor);

				accum_length += length;
				voxels[intervals[k].first] += vol_rel;
			}
		}
	}

	density_tex.data.resize(resx*resy*resz);

	// Clamp all density values to a sensible range
	for(unsigned i = 0; i < voxels.size(); ++i)
		density_tex.data[i] = cgv::math::clamp(voxels[i], 0.0f, 1.0f);

	density_tex.connect(new cgv::data::data_format(resx, resy, resz, cgv::type::info::TypeId::TI_FLT32, cgv::data::ComponentFormat::CF_R));
	density_tex.texture.create(ctx, density_tex.view, 0);
	density_tex.texture.set_min_filter(TF_LINEAR_MIPMAP_LINEAR);
	density_tex.texture.set_mag_filter(TF_LINEAR);
	density_tex.texture.set_wrap_s(TW_CLAMP_TO_BORDER);
	density_tex.texture.set_wrap_t(TW_CLAMP_TO_BORDER);
	density_tex.texture.set_wrap_r(TW_CLAMP_TO_BORDER);
	density_tex.texture.set_border_color(0.0f, 0.0f, 0.0f, 0.0f);
	density_tex.texture.generate_mipmaps(ctx);

	// Generate 3 cone sample directions to be used in the shader
	std::vector<vec3> sample_dirs(3);
	float cone_angle = 50.0f;

	float alpha2 = cgv::math::deg2rad(cone_angle / 2.0f);
	float beta = cgv::math::deg2rad(90.0f - (cone_angle / 2.0f));

	float a = sinf(alpha2);
	float dh = tanf(cgv::math::deg2rad(30.0f)) * a;

	float c = length(vec2(a, dh));

	float b = sqrtf(1 - c * c);

	// Set ambient occlusion attributes in tube render style
	tstyle.tex_offset = vbox_min;
	tstyle.tex_scaling = vec3(1.0f) / vbox_ext;
	tstyle.tex_coord_scaling = vec3(vres[max_ext_axis]) / vres;
	tstyle.texel_size = 1.0f / vres[max_ext_axis];
	tstyle.cone_angle_factor = 2.0f * sinf(alpha2) / sinf(beta);
	tstyle.sample_dirs.resize(3);
	tstyle.sample_dirs[0] = vec3(0.0f, b, c);
	tstyle.sample_dirs[1] = vec3(a, b, -dh);
	tstyle.sample_dirs[2] = vec3(-a, b, -dh);

	t.stop();
	std::cout << "done in " << t.seconds() << "s\n=====" << std::endl;
}

bool fiber_viewer::init(cgv::render::context& ctx) {

	cgv::render::ref_volume_renderer(ctx, 1);

	view_ptr = find_view_as_node();
	if(!view_ptr)
		return false;

	fb.cf = util::CF_FLT32;
	fb.create_and_validate(ctx, ctx.get_width(), ctx.get_height());

	cb.cf = util::CF_FLT32;
	cb.create_and_validate(ctx, ctx.get_width(), ctx.get_height());

	// Setup a color map
	color_map.values.clear();
	color_map.values.push_back(rgba(0.0f, 1.0f, 0.0f, 0.0f));
	color_map.values.push_back(rgba(0.0f, 0.0f, 1.0f, 0.5f));
	color_map.values.push_back(rgba(1.0f, 0.0f, 0.0f, 1.0f));

	set_dataset(ctx, true);

	cgv::data::data_format format;
	cgv::media::image::image_reader image(format);

	cgv::data::data_view tf_data;

	if(!image.read_image(resource_path + "inferno.bmp", tf_data))
		abort();

	tf_tex.create(ctx, tf_data, 0);
	tf_tex.set_min_filter(cgv::render::TextureFilter::TF_LINEAR);
	tf_tex.set_mag_filter(cgv::render::TextureFilter::TF_LINEAR);
	tf_tex.set_wrap_s(cgv::render::TextureWrap::TW_CLAMP_TO_EDGE);
	tf_tex.set_wrap_t(cgv::render::TextureWrap::TW_CLAMP_TO_EDGE);

	// Load render shaders
	if(!load_shader(ctx, tube_transparent_naive_prog, "tube_transparent_naive")) return false;
	//if(!load_shader(ctx, tube_transparent_al_prog, "tube_transparent_al")) return false;
	//if(!load_shader(ctx, tube_transparent_al_blend_prog, "tube_transparent_al_blend")) return false;

	// Load utility shaders
	if(!load_shader(ctx, final_blend_prog, "final_blend")) return false;
	if(!load_shader(ctx, expand_indices_prog, "expand_indices")) return false;
	if(!load_shader(ctx, clear_ssbo_prog, "clear_ssbo")) return false;

	create_buffers(ctx);

	// Set the background color
	background_color.alpha() = 1.0f; // Front-to-back blending

	ctx.set_bg_color(0.0f, 0.0f, 0.0f, 1.0f);

	return true;
}

void fiber_viewer::init_frame(cgv::render::context& ctx) {

	if(do_change_dataset) {
		do_change_dataset = false;
		set_dataset(ctx, false);
		update_member(&tstyle.radius);
		update_member(&tstyle.radius_scale);
	}

	if(do_rebuild_framebuffer) {
		do_rebuild_framebuffer = false;
		fb.destruct(ctx);
		cb.destruct(ctx);
	}

	if(fb.ensure(ctx) || cb.ensure(ctx) || do_rebuild_buffers) {
		do_rebuild_buffers = false;
		create_buffers(ctx);
	}

	if(do_create_density_volume) {
		do_create_density_volume = false;
		create_density_volume(ctx, dataset_bbox, tstyle.radius * tstyle.radius_scale);
	}

	if(do_change_color_source) {
		do_change_color_source = false;
		set_color_source(ctx);
	}
}

void fiber_viewer::draw(cgv::render::context& ctx) {

	vec3 eye(0.0f, 0.0f, 10.0f);
	vec3 view_dir(0.0f, 0.0f, 1.0f);
	if (view_ptr) {
		eye = view_ptr->get_eye();
		view_dir = view_ptr->get_view_dir();
	}

	tr.set_eye_position(eye);
	tr.set_view_direction(view_dir);

	switch (render_mode) {
	case RM_DEFERRED:
	{
		/*
			Deferred shading of fully opaque tube geometry. First rasterizes the geometry and
			saves color, position and normal in framebuffer attachments. In a second full
			screen pass the costly calculation of ambient occlusion is only performed for
			visible fragments.
		*/
		if (tr.enable(ctx)) {
			fb.fb.enable(ctx, 0, 1, 2);
			fb.fb.push_viewport(ctx);
			glClearColor(background_color.R(), background_color.G(), background_color.B(), 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			tr.rasterize(ctx, positions.size());

			fb.fb.disable(ctx);
			fb.fb.pop_viewport(ctx);

			fb.color.enable(ctx, 0);
			fb.position.enable(ctx, 1);
			fb.normal.enable(ctx, 2);
			density_tex.enable(ctx, 3);

			tr.shade(ctx);

			fb.color.disable(ctx);
			fb.position.disable(ctx);
			fb.normal.disable(ctx);
			density_tex.disable(ctx);

			tr.disable(ctx);
		}
	}
	break;
	case RM_TRANSPARENT_NAIVE:
	{
		/*
			Naive method for transparent rendering. This relies on drawing the segments sorted
			by distance to the camera and using default OpenGL blending. Overlapping geometry
			will most likely produce artifacts.
		*/

		// Sort the segments
		if (!disable_sorting)
			sort(ctx, positions_ssbo, segment_ibo, eye);

		set_transparent_shader_uniforms(ctx, view_ptr, tube_transparent_naive_prog);

		cb.fb.enable(ctx);
		glClear(GL_COLOR_BUFFER_BIT);

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_SRC_ALPHA);  // Front-to-back blending

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, positions_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, radii_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, colors_ssbo);

		density_tex.enable(ctx, 1);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

		glDrawElements(GL_LINES, positions.size(), GL_UNSIGNED_INT, (void*)0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		density_tex.disable(ctx);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);

		tube_transparent_naive_prog.disable(ctx);
		glDisable(GL_BLEND);

		cb.fb.disable(ctx);

		do_final_blend(ctx);

		glEnable(GL_DEPTH_TEST);
	}
	break;
	case RM_VOLUME:
	{
		vstyle.transfer_function_texture_unit = 1;
		cgv::render::volume_renderer& renderer = cgv::render::ref_volume_renderer(ctx);
		renderer.set_volume_texture(&fa_tex.texture);
		renderer.set_eye_position(eye);
		renderer.set_render_style(vstyle);

		tf_tex.enable(ctx, vstyle.transfer_function_texture_unit);
		renderer.render(ctx, 0, 0);
		tf_tex.disable(ctx);
	}
	break;
	//case RM_TRANSPARENT_ATOMIC_LOOP:
	//{
	//	/*
	//		Transparent rendering using an additional buffer with #alss entries during the
	//		rendering to temporarily store and sort individual fragments to their correct
	//		visibility order. Segments are still drawn sorted by distance.
	//	*/

	//	unsigned scratch_size_per_pixel = (unsigned)alss;

	//	// Clear the scratch buffer
	//	unsigned buffer_size = 2 * scratch_size_per_pixel * ctx.get_width() * ctx.get_height();
	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, scratch_buffer);
	//	clear_ssbo_prog.enable(ctx);
	//	clear_ssbo_prog.set_uniform(ctx, "size", buffer_size);
	//	clear_ssbo_prog.set_uniform(ctx, "clear_value", (int)0x00000000u);
	//	glDispatchCompute(ceil(buffer_size / 4), 1, 1);
	//	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	//	clear_ssbo_prog.disable(ctx);

	//	// Sort the segments
	//	if(!disable_sorting)
	//		sort(ctx, positions_ssbo, segment_ibo, eye);

	//	set_transparent_shader_uniforms(ctx, view_ptr, tube_transparent_al_prog);
	//	tube_transparent_al_prog.set_uniform(ctx, "scratch_size", scratch_size_per_pixel);

	//	cb.fb.enable(ctx);
	//	glClear(GL_COLOR_BUFFER_BIT);

	//	glDisable(GL_DEPTH_TEST);
	//	glEnable(GL_BLEND);
	//	glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_SRC_ALPHA);  // Front-to-back blending

	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, positions_ssbo);
	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, radii_ssbo);
	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, colors_ssbo);
	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, scratch_buffer);

	//	density_tex.enable(ctx, 1);
	//	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

	//	glDrawElements(GL_LINES, positions.size(), GL_UNSIGNED_INT, (void*)0);
	//	
	//	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	//	density_tex.disable(ctx);

	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);

	//	tube_transparent_al_prog.disable(ctx);

	//	tube_transparent_al_blend_prog.enable(ctx);
	//	tube_transparent_al_blend_prog.set_uniform(ctx, "viewport_dims", ivec2(ctx.get_width(), ctx.get_height()));
	//	tube_transparent_al_blend_prog.set_uniform(ctx, "scratch_size", scratch_size_per_pixel);

	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, scratch_buffer);
	//	
	//	// Blend the fragments remaining in the temporary (scratch) buffer to the screen
	//	for(int i = 0; i < scratch_size_per_pixel; ++i) {
	//		tube_transparent_al_blend_prog.set_uniform(ctx, "idx", (int)scratch_size_per_pixel - i - 1);
	//		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	//	}

	//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);

	//	glDisable(GL_BLEND);
	//	tube_transparent_al_blend_prog.disable(ctx);
	//	cb.fb.disable(ctx);

	//	do_final_blend(ctx);

	//	glEnable(GL_DEPTH_TEST);
	//}
	//break;
	//}
	}
}

void fiber_viewer::set_color_source(const context& ctx) {

	std::vector<rgba>& color_data = std::vector<rgba>(0);

	switch(color_source) {
	case CS_ATTRIBUTE:
		color_data = colors_attribute;
		break;
	case CS_MIDPOINT:
		color_data = colors_midpoint;
		break;
	case CS_SEGMENT:
		color_data = colors_segment;
		break;
	case CS_COOLWARM:
		color_data = colors_coolwarm;
		break;
	case CS_EXTENDED_KINDLMANN:
		color_data = colors_extended_kindlmann;
		break;
	case CS_EXTENDED_BLACKBODY:
		color_data = colors_extended_blackbody;
		break;
	case CS_BLACKBODY:
		color_data = colors_blackbody;
		break;
	case CS_ISORAINBOW:
		color_data = colors_isorainbow;
		break;
	case CS_BOYS:
		color_data = colors_boys;
		break;

	}

	if(color_data.size() > 0) {
		tr.set_color_array(ctx, color_data);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, colors_ssbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER, color_data.size() * sizeof(rgba), (void*)color_data.data(), GL_DYNAMIC_COPY);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
}

bool fiber_viewer::load_shader(context& ctx, shader_program& prog, std::string name, std::string defines) {

	if(prog.is_created()) {
		prog.destruct(ctx);
	}

	if(!prog.is_created()) {
		if(!prog.build_program(ctx, name + ".glpr", true, defines)) {
			std::cerr << "ERROR in fiber_viewer::init() ... could not build program " << name << ".glpr" << std::endl;
			return false;
		}
	}
	return true;
}

void fiber_viewer::sort(context& ctx, const GLuint position_buffer, const GLuint index_buffer, const vec3& eye_position) {

	sorter.sort(ctx, positions_ssbo, segment_ibo, eye_position);

	unsigned segment_count = positions.size() / 2;

	expand_indices_prog.enable(ctx);
	expand_indices_prog.set_uniform(ctx, "n", segment_count - sorter.get_padding());
	expand_indices_prog.set_uniform(ctx, "n_padded", segment_count);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, segment_ibo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ibo);

	glDispatchCompute(sorter.get_group_size(), 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	expand_indices_prog.disable(ctx);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
}

void fiber_viewer::set_transparent_shader_uniforms(context& ctx, view* view_ptr, shader_program& prog) {

	vec3 eye_pos(0.0f);
	vec3 view_dir(0.0f);

	if(view_ptr) {
		eye_pos = view_ptr->get_eye();
		view_dir = view_ptr->get_view_dir();
	}

	prog.enable(ctx);
	prog.set_uniform(ctx, "use_global_radius", radii.size() != positions.size());
	prog.set_uniform(ctx, "radius", tstyle.radius);
	prog.set_uniform(ctx, "radius_scale", tstyle.radius_scale);
	prog.set_uniform(ctx, "eye_pos", eye_pos);
	prog.set_uniform(ctx, "view_dir", view_dir);
	prog.set_uniform(ctx, "viewport_dims", ivec2(ctx.get_width(), ctx.get_height()));
	prog.set_uniform(ctx, "alpha_scale", alpha_scale);
	prog.set_uniform(ctx, "disable_clipping", disable_clipping);
	
	ctx.set_material(tstyle.material);
	ctx.set_color(tstyle.surface_color);
	prog.set_uniform(ctx, "map_color_to_material", int(tstyle.map_color_to_material));
	prog.set_uniform(ctx, "culling_mode", int(tstyle.culling_mode));
	prog.set_uniform(ctx, "illumination_mode", int(tstyle.illumination_mode));

	if(tstyle.enable_ambient_occlusion) {
		prog.set_uniform(ctx, "ao_offset", tstyle.ao_offset);
		prog.set_uniform(ctx, "ao_distance", tstyle.ao_distance);
		prog.set_uniform(ctx, "ao_strength", tstyle.ao_strength);
		prog.set_uniform(ctx, "density_tex_offset", tstyle.tex_offset);
		prog.set_uniform(ctx, "density_tex_scaling", tstyle.tex_scaling);
		prog.set_uniform(ctx, "tex_coord_scaling", tstyle.tex_coord_scaling);
		prog.set_uniform(ctx, "texel_size", tstyle.texel_size);
		prog.set_uniform(ctx, "cone_angle_factor", tstyle.cone_angle_factor);
		prog.set_uniform_array(ctx, "sample_dirs", tstyle.sample_dirs);

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

		prog.set_uniform(ctx, "inverse_modelview_mat", inv(MV));
		prog.set_uniform(ctx, "inverse_normal_mat", NM);
	}
}

/*
	Does a fullscreen pass to blend the framebuffer contents to the screen on top of the
	background color and do gamma correction.
*/
void fiber_viewer::do_final_blend(context& ctx) {

	final_blend_prog.enable(ctx);
	cb.color.enable(ctx, 0);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	cb.color.disable(ctx);
	final_blend_prog.disable(ctx);
}

void fiber_viewer::create_buffers(const context& ctx) {

	// Delete old buffers
	if(scratch_buffer != 0) {
		glDeleteBuffers(1, &scratch_buffer);
		scratch_buffer = 0;
	}

	unsigned width = ctx.get_width();
	unsigned height = ctx.get_height();

	// Generate the atomic loop scratch buffer
	glGenBuffers(1, &scratch_buffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, scratch_buffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint64_t) * (unsigned)alss * width * height, NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void fiber_viewer::create_gui() {
	add_decorator("Line Viewer", "heading", "level=2");

	//add_member_control(this, "Dataset", dataset, "dropdown", "enums='test,brain_segment,whole_brain'");
	add_gui("Dataset", dataset_filename, "file_name", "title='select dataset file';filter='tractography files:*.trk|All Files:*.*'");
	add_member_control(this, "Color mapping", color_source, "dropdown", "enums='attribute,midpoint,segment,coolwarm,e_kindlmann,e_blackbody,blackbody,isorainbow,boysurface'");
	add_member_control(this, "Render mode", render_mode, "dropdown", "enums='deferred,transparent naive,transparent atomic loop,volume'");
	add_member_control(this, "FB format", fb.cf, "dropdown", "enums='flt32,uint8'");
	add_member_control(this, "Scratch size", alss, "dropdown", "enums='1,2,4,8,16,32'");

	add_member_control(this, "Voxel resolution", voxel_resolution, "dropdown", "enums='8,16,32,64,128,256,512'");

	add_member_control(this, "Disable sorting", disable_sorting, "check", "");
	add_member_control(this, "Disable clipping", disable_clipping, "check", "");

	if(begin_tree_node("Render settings", tstyle, true)) {
		align("\a");
		add_gui("tstyle", tstyle);
		add_member_control(this, "Alpha scale", alpha_scale, "value_slider", "min=0.0;step=0.0001;max=1.0;ticks=true");
		align("\b");
		end_tree_node(tstyle);
	}

	if(begin_tree_node("Volume rendering", vstyle, false)) {
		align("\a");
		add_gui("vstyle", vstyle);
		align("\b");
		end_tree_node(vstyle);
	}
}

//Alaleh's
void rp2ColorMapping(float startP[3], float endP[3], float rgb[3])
{
	float v[3];

	for (int k = 0; k <= 2; k++)
		v[k] = endP[k] - startP[k];

	//do a normalization, just in case
	float l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	v[0] = v[0] / l;
	v[1] = v[1] / l;
	v[2] = v[2] / l;

	float x = v[0];
	float y = v[1];
	float z = v[2];

	float xx2 = v[0] * v[0];
	float xx3 = v[0] * v[0] * v[0];
	float yy2 = v[1] * v[1];
	float yy3 = v[1] * v[1] * v[1];
	float zz2 = v[2] * v[2];
	float zz3 = v[2] * v[2] * v[2];
	float zz4 = v[2] * v[2] * v[2] * v[2];
	float xy = v[0] * v[1];
	float xz = v[0] * v[2];
	float yz = v[1] * v[2];

	float hh1 = .5 * (3 * zz2 - 1) / 1.58;
	float hh2 = 3 * xz / 2.745;
	float hh3 = 3 * yz / 2.745;
	float hh4 = 1.5 * (xx2 - yy2) / 2.745;
	float hh5 = 6 * xy / 5.5;
	float hh6 = (1 / 1.176) * .125 * (35 * zz4 - 30 * zz2 + 3);
	float hh7 = 2.5 * x * (7 * zz3 - 3 * z) / 3.737;
	float hh8 = 2.5 * y * (7 * zz3 - 3 * z) / 3.737;
	float hh9 = ((xx2 - yy2) * 7.5 * (7 * zz2 - 1)) / 15.85;
	float hh10 = ((2 * xy) * (7.5 * (7 * zz2 - 1))) / 15.85;
	float hh11 = 105 * (4 * xx3 * z - 3 * xz * (1 - zz2)) / 59.32;
	float hh12 = 105 * (-4 * yy3 * z + 3 * yz * (1 - zz2)) / 59.32;

	float s0 = -23.0;
	float s1 = 227.9;
	float s2 = 251.0;
	float s3 = 125.0;

	auto SS = [](float NA, float ND) -> float
	{
		return NA * sin(ND * 3.141592 / 180);
	};

	auto CC = [](float NA, float ND) -> float
	{
		return NA * cos(ND * 3.141592 / 180);
	};

	float ss23 = SS(2.71, s0);
	float cc23 = CC(2.71, s0);
	float ss45 = SS(2.12, s1);
	float cc45 = CC(2.12, s1);
	float ss67 = SS(.972, s2);
	float cc67 = CC(.972, s2);
	float ss89 = SS(.868, s3);
	float cc89 = CC(.868, s3);

	float X = 0.0;
	X = X + hh2 * cc23;
	X = X + hh3 * ss23;

	X = X + hh5 * cc45;
	X = X + hh4 * ss45;

	X = X + hh7 * cc67;
	X = X + hh8 * ss67;

	X = X + hh10 * cc89;
	X = X + hh9 * ss89;

	float Y = 0.0;
	Y = Y + hh2 * -ss23;
	Y = Y + hh3 * cc23;

	Y = Y + hh5 * -ss45;
	Y = Y + hh4 * cc45;

	Y = Y + hh7 * -ss67;
	Y = Y + hh8 * cc67;

	Y = Y + hh10 * -ss89;
	Y = Y + hh9 * cc89;


	float Z = 0.0;
	Z = Z + hh1 * -2.8;
	Z = Z + hh6 * -0.5;
	Z = Z + hh11 * 0.3;
	Z = Z + hh12 * -2.5;

	// scale and normalize to fit in the rgb space
	float w_x = 4.1925;
	float trl_x = -2.0425;

	float w_y = 4.0217;
	float trl_y = -1.8541;

	float w_z = 4.0694;
	float trl_z = -2.1899;


	rgb[0] = 0.9 * abs(((X - trl_x) / w_x)) + 0.05;
	rgb[1] = 0.9 * abs(((Y - trl_y) / w_y)) + 0.05;
	rgb[2] = 0.9 * abs(((Z - trl_z) / w_z)) + 0.05;
}//Alaleh's

#include "lib_begin.h"

#include <cgv/base/register.h>

extern CGV_API cgv::base::object_registration<fiber_viewer> fiber_viewer_reg("fiber_viewer");
