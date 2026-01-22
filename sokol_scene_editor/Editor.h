#define SOKOL_GLCORE
#include "sokol/sokol_engine.h"
#include "sokol/include/sokol_gfx.h"
#include "sokol/include/sokol_glue.h"
#include <iostream>
#include <vector>

#include "shd.glsl.h"

#include "cmn/math/v3d.h"
#include "cmn/math/mat4.h"
#include "utils.h"
#include "mesh.h"
#include "linemesh.h"
#include "Object.h"
#include "texture_utils.h"

static cmn::vf3d polar3D(float yaw, float pitch) {
	return {
		std::sin(yaw) * std::cos(pitch),
		std::sin(pitch),
		std::cos(yaw) * std::cos(pitch)
	};
}



struct
{
	cmn::vf3d pos{ 0,2,2 };
	cmn::vf3d dir;
	float yaw = 0;
	float pitch = 0;
	cmn::mat4 proj, view;
	cmn::mat4 view_proj;
}cam;

struct Light
{
	cmn::vf3d pos;
	sg_color col;

};

static float prepDistToRay(const cmn::vf3d& pt, const cmn::vf3d& orig, const cmn::vf3d& dir)
{
	return dir.cross(pt - orig).mag_sq();
}

struct Editor : SokolEngine
{
	bool use_gizmo = false;
	cmn::vf3d gizmo_drag_orig;
	const float gizmo_axis_sz = 1.3f, gizmo_margin = 0.3f, gizmo_square_sz = 0.7f;
	enum struct GizmoMode {
		None,
		XAxis,
		YAxis,
		ZAxis,
		XYPlane,
		YZPlane,
		ZXPlane
	}gizmo_mode = GizmoMode::None;


	sg_pipeline default_pip{};
	sg_pipeline line_pip{};

	sg_sampler sampler{};
	sg_pass_action display_pass_action{};

	std::vector<Light> lights;
	Object terrain;
	Object gizmoObj;
	std::vector<Object> objects;
	sg_view tex_blank{};
	sg_view tex_uv{};
	cmn::vf3d mouse_dir, prev_mouse_dir;

	const std::vector<std::string> Structurefilenames{
		"assets/models/desert.txt",
		"assets/models/house.txt",
	};

	const std::vector<std::string> texturefilenames{
		"assets/poust_1.png",
		"assets/sandtexture.png",

	};


#pragma region SETUP HELPERS
	void setupEnvironment() {
		sg_desc desc{};
		desc.environment = sglue_environment();
		sg_setup(desc);
	}

	void setupLights()
	{
		//white
		lights.push_back({ {-1,3,1},{1,1,1,1} });
		//mainlight = &lights.back();

	}

	//primitive textures to debug with
	void setupTextures() {
		tex_blank = makeBlankTexture();
		tex_uv = makeUVTexture(512, 512);
	}

	//if texture loading fails, default to uv tex.
	sg_view getTexture(const std::string& filename) {
		sg_view tex;
		auto status = makeTextureFromFile(tex, filename);
		if (!status.valid) tex = tex_uv;
		return tex;
	}

	void setupBillboard() {

		std::vector<cmn::vf3d> coords
		{
			{0,0,0},
			{0,-1,0},
			{-1,0,0}
		};

		std::vector<cmn::vf3d> rots
		{
			{0,0,0},
			{0,0,0},
			{0,0,0}
		};

		for (int i = 0; i < coords.size(); i++)
		{
			Object obj;
			Mesh& m = obj.mesh;
			m.verts = {
				{{-.5f, .5f, 0}, {0, 0, 1}, {0, 0}},//tl
				{{.5f, .5f, 0}, {0, 0, 1}, {1, 0}},//tr
				{{-.5f, -.5f, 0}, {0, 0, 1}, {0, 1}},//bl
				{{.5f, -.5f, 0}, {0, 0, 1}, {1, 1}},//br

			};
			m.tris = {
				{0, 2, 1},
				{1, 2, 3},

			};
			

			obj.translation = coords[i];
			obj.rotation = rots[i];
			obj.isbillboard = true;
			obj.draggable = true;
			m.updateVertexBuffer();
			m.updateIndexBuffer();

			obj.tex = makeColorTexture(0xffffffff);
			obj.num_x = 1, obj.num_y = 1;
			obj.num_ttl = obj.num_x * obj.num_y;
			objects.push_back(obj);
		}
	}


	void setupSampler() {
		sg_sampler_desc sampler_desc{};
		sampler = sg_make_sampler(sampler_desc);
	}

	//clear to bluish
	void setupDisplayPassAction() {
		display_pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
		display_pass_action.colors[0].clear_value = { .25f, .45f, .65f, 1.f };
	}

	void setupDefaultPipeline() {
		sg_pipeline_desc pipeline_desc{};
		pipeline_desc.layout.attrs[ATTR_default_v_pos].format = SG_VERTEXFORMAT_FLOAT3;
		pipeline_desc.layout.attrs[ATTR_default_v_norm].format = SG_VERTEXFORMAT_FLOAT3;
		pipeline_desc.layout.attrs[ATTR_default_v_uv].format = SG_VERTEXFORMAT_FLOAT2;
		pipeline_desc.shader = sg_make_shader(default_shader_desc(sg_query_backend()));
		pipeline_desc.index_type = SG_INDEXTYPE_UINT32;
		pipeline_desc.cull_mode = SG_CULLMODE_FRONT;
		pipeline_desc.depth.write_enabled = true;
		pipeline_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
		default_pip = sg_make_pipeline(pipeline_desc);
	}

	void setupLinePipeline()
	{
		sg_pipeline_desc pip_desc{};
		pip_desc.layout.attrs[ATTR_line_v_pos].format = SG_VERTEXFORMAT_FLOAT3;
		pip_desc.layout.attrs[ATTR_line_v_col].format = SG_VERTEXFORMAT_FLOAT4;
		pip_desc.shader = sg_make_shader(line_shader_desc(sg_query_backend()));
		pip_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
		pip_desc.index_type = SG_INDEXTYPE_UINT32;
		pip_desc.depth.write_enabled = true;
		pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
		line_pip = sg_make_pipeline(pip_desc);
	}

	void setupObjects()
	{
		std::vector<cmn::vf3d> coords
		{
			{0.0f, -4.0f, 0.0f},
			{0.0f, 0.0f, 0.0f},
			//{51.31f, 1.67f, 41.95f}, 
			//{62.04f, 1.67f, -5.12f}, 
			//{12.09f, 2.16f, -38.03f},
			//{-38.35f, 2.63f, -33.38f},
			//{-52.99f, 1.43f, 16.84f},
		};

		std::vector<std::uint32_t> colors
		{
			0xFFFFFFFF,  //white
			0xFF0000ff,  //blue
			0xFFffFFff,   //white
			0xFFff0000,   //red
			0xFFffffff,   //white
			0xFF00FF00,   // green
			0xFF0000ff,   //blue
		};



		{
			Object b;
			Mesh& m = b.mesh;
			auto status = Mesh::loadFromOBJ(m, Structurefilenames[0]);
			if (!status.valid) m = Mesh::makeCube();
			b.scale = { 1,1,1 };

			
			b.tex = getTexture(texturefilenames[0]);
			terrain = Object(m, b.tex);
			terrain.translation = { 0.0f, -5.0f, 0.0f };
			terrain.updateMatrixes();
		}

		{
			Object b;
			b.tex = makeColorTexture(0xffff0000);
			gizmoObj = Object(Mesh::makeCube(), b.tex);
			gizmoObj.scale = { 0.2f,0.2f,0.2f };
			gizmoObj.translation = { 0.0f,0.0f,0.0f };
			gizmoObj.updateMatrixes();
		}
	}


#pragma endregion
	void userCreate() override
	{
		setupEnvironment();
		setupLinePipeline();
		setupTextures();
		setupSampler();
		setupLights();
		setupObjects();
		setupBillboard();
		setupDisplayPassAction();

		setupDefaultPipeline();
	}


#pragma region UPDATE HELPERS
	void updateCameraMatrixes() {
		//dont look while gizmoing
		if (gizmo_mode != GizmoMode::None) return;
		cmn::mat4 look_at = cmn::mat4::makeLookAt(cam.pos, cam.pos + cam.dir, { 0, 1, 0 });
		cam.view = cmn::mat4::inverse(look_at);

		//cam proj can change with window resize
		float asp = sapp_widthf() / sapp_heightf();
		cam.proj = cmn::mat4::makePerspective(90, asp, .001f, 1000.f);

		cam.view_proj = cmn::mat4::mul(cam.proj, cam.view);
	}

	void handleCameraLooking(float dt) {
		//dont look while gizmoing
		if (gizmo_mode != GizmoMode::None) return;
		//left/right
		if (getKey(SAPP_KEYCODE_LEFT).held) cam.yaw += dt;
		if (getKey(SAPP_KEYCODE_RIGHT).held) cam.yaw -= dt;

		//up/down
		if (getKey(SAPP_KEYCODE_UP).held) cam.pitch += dt;
		if (getKey(SAPP_KEYCODE_DOWN).held) cam.pitch -= dt;

		//clamp camera pitch
		if (cam.pitch > Pi / 2) cam.pitch = Pi / 2 - .001f;
		if (cam.pitch < -Pi / 2) cam.pitch = .001f - Pi / 2;


	}

	void handleCameraMovement(float dt) {



		//move up, down
		if (getKey(SAPP_KEYCODE_SPACE).held) cam.pos.y += 4.f * dt;
		if (getKey(SAPP_KEYCODE_LEFT_SHIFT).held) cam.pos.y -= 4.f * dt;


		//move forward, backward
		cmn::vf3d fb_dir(std::sin(cam.yaw), 0, std::cos(cam.yaw));
		if (getKey(SAPP_KEYCODE_W).held) cam.pos += 5.f * dt * fb_dir;
		if (getKey(SAPP_KEYCODE_S).held) cam.pos -= 3.f * dt * fb_dir;

		//move left, right
		cmn::vf3d lr_dir(fb_dir.z, 0, -fb_dir.x);
		if (getKey(SAPP_KEYCODE_A).held) cam.pos += 4.f * dt * lr_dir;
		if (getKey(SAPP_KEYCODE_D).held) cam.pos -= 4.f * dt * lr_dir;

	}

	void handleUserInput(float dt) {
		handleCameraLooking(dt);
		//polar to cartesian
		cam.dir = polar3D(cam.yaw, cam.pitch);
		//if (getKey(SAPP_KEYCODE_R).held) mainlight->pos = cam.pos;
		//toggle shape outlines
		//if (getKey(SAPP_KEYCODE_O).pressed) render_outlines ^= true;

		handleCameraMovement(dt);
	}


	//make billboard always point at camera.
	void updateBillboard(Object& obj, float dt) {
		//move with player 
		cmn::vf3d eye_pos = obj.translation;
		cmn::vf3d target = cam.pos;

		cmn::vf3d y_axis(0, 1, 0);
		cmn::vf3d z_axis = (target - eye_pos).norm();
		cmn::vf3d x_axis = y_axis.cross(z_axis).norm();
		y_axis = z_axis.cross(x_axis);

		//slightly different than makeLookAt.
		cmn::mat4& m = obj.model;
		m(0, 0) = x_axis.x, m(0, 1) = y_axis.x, m(0, 2) = z_axis.x, m(0, 3) = eye_pos.x;
		m(1, 0) = x_axis.y, m(1, 1) = y_axis.y, m(1, 2) = z_axis.y, m(1, 3) = eye_pos.y;
		m(2, 0) = x_axis.z, m(2, 1) = y_axis.z, m(2, 2) = z_axis.z, m(2, 3) = eye_pos.z;
		m(3, 3) = 1;

	
		
	}

#pragma region GIZMO HELPERS

	void gizmoBillboardUpdates()
	{
		if (!use_gizmo) return;

		const auto& g = gizmoObj.translation;
		const auto& a = gizmo_axis_sz;
		const auto& m = gizmo_margin;
		const auto& s = gizmo_square_sz;

		sg_color white = { 1, 1, 1, 1 };
		sg_color blue = { 1, 0, 0, 1 };
		sg_color green = { 1, 0, 1, 0 };
		sg_color purple = { 1, 1, 0, 1 };

		Parallelogram(
			objects[0],
			g + cmn::vf3d(m, m, 0),
			cmn::vf3d(s, 0, 0), cmn::vf3d(0, s, 0),
			gizmo_mode == GizmoMode::XYPlane ? white : blue
		);
		
		Parallelogram(
			objects[1],
			g + cmn::vf3d(0, m, m),
			cmn::vf3d(0, s, 0), cmn::vf3d(0, 0, s),
			gizmo_mode == GizmoMode::XYPlane ? white : purple
		);
		
		Parallelogram(
			objects[2],
			g + cmn::vf3d(m, 0, m),
			cmn::vf3d(0, 0, s), cmn::vf3d(s, 0, 0),
			gizmo_mode == GizmoMode::XYPlane ? white : green
		);
		
	}

	void Parallelogram(Object& obj,const cmn::vf3d& pos, const cmn::vf3d& a, const cmn::vf3d& b,sg_color& col)
	{
		cmn::vf3d x_axis = pos + a;
		cmn::vf3d y_axis = pos + b;
		cmn::vf3d z_axis = pos + a + b;

		cmn::mat4& m = obj.model;
		m(0, 0) = x_axis.x, m(0, 1) = y_axis.x, m(0, 2) = z_axis.x, m(0, 3) = pos.x;
		m(1, 0) = x_axis.y, m(1, 1) = y_axis.y, m(1, 2) = z_axis.y, m(1, 3) = pos.y;
		m(2, 0) = x_axis.z, m(2, 1) = y_axis.z, m(2, 2) = z_axis.z, m(2, 3) = pos.z;
		m(3, 3) = 1;
		 
	}

	void handleMouseRays()
	{
		prev_mouse_dir = mouse_dir;

		//unproject mouse point
		float ndc_x = 2.f * mouse_x / sapp_width() - 1;
		float ndc_y = 1 - 2.f * mouse_y / sapp_height();
		cmn::vf3d clip(ndc_x, ndc_y, 1);
		float w = 1;
		cmn::mat4 inv_vp = cmn::mat4::inverse(cmn::mat4::mul(cam.proj, cam.view));
		cmn::vf3d world = cmn::matMulVec(inv_vp, clip, w);
		world /= w;
		mouse_dir = (world - cam.pos).norm();

	}

	float rayIntersectTri(const cmn::vf3d & orig, const cmn::vf3d& dir, const cmn::vf3d& t0, const cmn::vf3d& t1, const cmn::vf3d& t2,
		float* uptr = nullptr, float* vptr = nullptr)
	{
		static const float epsilon = 1e-6f;

		cmn::vf3d a = dir;
		cmn::vf3d b = t0 - t1;
		cmn::vf3d c = t0 - t2;
		cmn::vf3d d = t0 - orig;
		cmn::vf3d bxc = b.cross(c);
		float det = a.dot(bxc);

		//parallel
		if (std::abs(det) < epsilon) return -1;

		cmn::vf3d f = c.cross(a) / det;
		float u = f.dot(d);
		if (uptr) *uptr = u;

		cmn::vf3d g = a.cross(b) / det;
		float v = g.dot(d);
		if (vptr) *vptr = v;

		//within unit uv triangle
		if (u < 0 || u>1) return -1;
		if (v < 0 || v>1) return -1;
		if (u + v > 1) return -1;

		//get t
		cmn::vf3d e = bxc / det;
		float t = e.dot(d);

		//behind ray
		if (t < 0) return -1;

		return t;
	}

	cmn::vf3d segIntersectPlane(const cmn::vf3d& a, const cmn::vf3d& b, const cmn::vf3d& ctr,
		const cmn::vf3d& norm, float* tp = nullptr)
	{
		float t = norm.dot(ctr - a) / norm.dot(b - a);
		if (tp) *tp = t;
		return a + t * (b - a);
	}

	void handleGizmoDragBegin()
	{
		const auto& g = gizmoObj.translation;
		const auto& a = gizmo_axis_sz;
		const auto& m = gizmo_margin;
		const auto& s = gizmo_square_sz;

		handleGizmoDragEnd();
		gizmo_drag_orig = g;

		// is mouse over axis ends?
		int n = 0;
		if (prepDistToRay(g + cmn::vf3d(a, 0, 0), cam.pos, mouse_dir) < m) gizmo_mode = GizmoMode::XAxis;
		if (prepDistToRay(g + cmn::vf3d(0, a, 0), cam.pos, mouse_dir) < m) gizmo_mode = GizmoMode::YAxis;
		if (prepDistToRay(g + cmn::vf3d(0, 0, a), cam.pos, mouse_dir) < m) gizmo_mode = GizmoMode::ZAxis;

		//is mouse over squares?
		cmn::vf3d rel = g - cam.pos;
		float u, v;
		//might have to use matmulvel
		rayIntersectTri({ 0,0,0 }, mouse_dir, 
			rel + cmn::vf3d(m, m, 0),
			rel + cmn::vf3d(m + s, m, 0),
			rel + cmn::vf3d(m, m + s, 0),
			&u, &v);
		if (u > 0 && v > 0 && u < 1 && v < 1) gizmo_mode = GizmoMode::XYPlane;

		rayIntersectTri({ 0,0,0 }, mouse_dir,
			rel + cmn::vf3d(0, m, m), 
			rel + cmn::vf3d(0, m + s, m),
			rel + cmn::vf3d(0, m, m + s),
			&u, &v);
		if (u > 0 && v > 0 && u < 1 && v < 1) gizmo_mode = GizmoMode::YZPlane;

		rayIntersectTri({ 0,0,0 }, mouse_dir,
			rel + cmn::vf3d(m, 0, m),
			rel + cmn::vf3d(m, 0, m + s),
			rel + cmn::vf3d(m + s, 0, m),
			&u, &v);
		if (u > 0 && v > 0 && u < 1 && v < 1) gizmo_mode = GizmoMode::ZXPlane;

		
		

	}

	void handleGizmoDragUpdate()
	{
		//which plane/axis to constrain motion to?
		cmn::vf3d axis, norm;
		bool constrain = false;
		switch (gizmo_mode)
		{
		default: return;
		case GizmoMode::XAxis: constrain = true; axis = { 1,0,0 }, norm = { 0,1,0 }; break;
		case GizmoMode::YAxis: constrain = true; axis = { 0,1,0 }, norm = { 0,0,1 }; break;
		case GizmoMode::ZAxis: constrain = true; axis = { 0,0,1 }, norm = { 1,0,0 }; break;
		case GizmoMode::XYPlane: norm = { 0,0,1 }; break;
		case GizmoMode::YZPlane: norm = { 1,0,0 }; break;
		case GizmoMode::ZXPlane: norm = { 0,1,0 }; break;
		}

		auto& g = gizmoObj.translation;

		//intersect prev & curr mouse rays to plane
		cmn::vf3d curr_pt = segIntersectPlane(cam.pos, cam.pos + mouse_dir, gizmo_drag_orig, norm);
		cmn::vf3d prev_pt = segIntersectPlane(cam.pos, cam.pos + prev_mouse_dir, gizmo_drag_orig, norm);

		//move by delta
		cmn::vf3d delta = curr_pt - prev_pt;

		if (constrain) g += axis.dot(delta) * axis;
		else g += delta;
        

		//gizmoObj.updateMatrixes();
	}

	void handleGizmoDragEnd()
	{
		gizmo_mode = GizmoMode::None;
	}


	void handleGizmoUpdate()
	{
		if (getKey(SAPP_KEYCODE_G).pressed) use_gizmo ^= true;

		if (!use_gizmo) return;

		const auto grab_action = getMouse(SAPP_MOUSEBUTTON_LEFT);
		if (grab_action.pressed) handleGizmoDragBegin();
		if (grab_action.held) handleGizmoDragUpdate();
		if (grab_action.released) handleGizmoDragEnd();

	}
#pragma endregion

#pragma endregion
	void userUpdate(float dt)
	{
		handleUserInput(dt);

		handleMouseRays();

		updateCameraMatrixes();
		gizmoBillboardUpdates();
		handleGizmoUpdate();
		

		//for (auto& obj : objects)
		//{
		//	//if (obj.isbillboard)
		//	//{
		//	//	updateBillboard(obj, dt);
		//	//
		//	//}
		//	
		//
		//}
	}

#pragma region RENDER HELPERS


#pragma region GEOMETRY HELPERS




#pragma endregion

	void renderObjects(Object& obj, const cmn::mat4& view_proj) {
		sg_apply_pipeline(default_pip);
		sg_bindings bind{};
		bind.vertex_buffers[0] = obj.mesh.vbuf;
		bind.index_buffer = obj.mesh.ibuf;
		bind.samplers[SMP_default_smp] = sampler;
		bind.views[VIEW_default_tex] = obj.tex;

		sg_apply_bindings(bind);

		//pass transformation matrix
		cmn::mat4 mvp = cmn::mat4::mul(view_proj, obj.model);
		vs_params_t vs_params{};
		std::memcpy(vs_params.u_model, obj.model.m, sizeof(vs_params.u_model));
		std::memcpy(vs_params.u_mvp, mvp.m, sizeof(mvp.m));
		sg_apply_uniforms(UB_vs_params, SG_RANGE(vs_params));

		//render entire texture.
		//fs_params_t fs_params{};
		//lighting test
		fs_params_t fs_params{};
		{

			fs_params.u_num_lights = lights.size();
			int idx = 0;
			for (const auto& l : lights)
			{
				fs_params.u_light_pos[idx][0] = l.pos.x;
				fs_params.u_light_pos[idx][1] = l.pos.y;
				fs_params.u_light_pos[idx][2] = l.pos.z;
				fs_params.u_light_col[idx][0] = l.col.r;
				fs_params.u_light_col[idx][1] = l.col.g;
				fs_params.u_light_col[idx][2] = l.col.b;
				idx++;
			}
		}

		fs_params.u_view_pos[0] = cam.pos.x;
		fs_params.u_view_pos[1] = cam.pos.y;
		fs_params.u_view_pos[2] = cam.pos.z;
		//sg_apply_uniforms(UB_fs_params, SG_RANGE(fs_params));


		fs_params.u_tl[0] = 0, fs_params.u_tl[1] = 0;
		fs_params.u_br[0] = 1, fs_params.u_br[1] = 1;
		sg_apply_uniforms(UB_fs_params, SG_RANGE(fs_params));

		sg_draw(0, 3 * obj.mesh.tris.size(), 1);
	}

	//void renderObjectOutlines()
	//{
	//
	//	sg_apply_pipeline(line_pip);
	//
	//	sg_bindings bind{};
	//	bind.vertex_buffers[0] = nodelines.vbuf;
	//	bind.index_buffer = nodelines.ibuf;
	//	sg_apply_bindings(bind);
	//
	//	vs_line_params_t vs_line_params{};
	//	mat4 mvp = mat4::mul(cam.view_proj, mat4::makeIdentity());
	//	std::memcpy(vs_line_params.u_mvp, mvp.m, sizeof(vs_line_params.u_mvp));
	//	sg_apply_uniforms(UB_vs_line_params, SG_RANGE(vs_line_params));
	//
	//	sg_draw(0, 2 * nodelines.lines.size(), 1);
	//
	//
	//}

#pragma endregion

	void userRender()
	{
		sg_pass pass{};
		pass.action = display_pass_action;
		pass.swapchain = sglue_swapchain();
		sg_begin_pass(pass);

		renderObjects(terrain, cam.view_proj);
		//renderObjects(gizmoObj, cam.view_proj);
		for (auto& obj : objects)
		{
			renderObjects(obj, cam.view_proj);
		}

		

		sg_end_pass();

		sg_commit();
	}

};