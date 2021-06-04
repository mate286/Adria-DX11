#include "../Threading/TaskSystem.h"
#include "Engine.h"
#include "Window.h"
#include "Events.h"
#include "../Math/Constants.h"
#include "../Logging/Logger.h"
#include "../Graphics/GraphicsCoreDX11.h"
#include "../Rendering/Renderer.h"
#include "../Editor/GUI.h"
#include "../Rendering/EntityLoader.h"
#include "../Utilities/Random.h"
#include "../Utilities/Timer.h"
#include "../Audio/AudioSystem.h"

namespace adria
{
	using namespace tecs;

	Engine::Engine(engine_init_t const& init)  : vsync{ init.vsync }, event_queue {}, input{ event_queue }, camera_manager{ input }
	{
		Log::Initialize(init.log_file);
		TaskSystem::Initialize();

		gfx = std::make_unique<GraphicsCoreDX11>(Window::Handle());
		renderer = std::make_unique<Renderer>(reg, gfx.get(), Window::Width(), Window::Height());
		entity_loader = std::make_unique<EntityLoader>(reg, gfx->Device(), renderer->GetTextureManager());
		
		camera_desc_t camera_desc{};
		camera_desc.aspect_ratio = static_cast<f32>(Window::Width()) / Window::Height();
		camera_desc.near_plane = 1.0f;
		camera_desc.far_plane = 1000.0f;
		camera_desc.fov = pi_div_4<f32>;
		camera_desc.position_x = 0.0f;
		camera_desc.position_y = 25.0f;
		camera_desc.position_z = 0.0f;
		camera_manager.AddCamera(camera_desc);
		InitializeScene(init.load_default_scene);

		event_queue.Subscribe<ResizeEvent>([this](ResizeEvent const& e) 
			{
				camera_manager.OnResize(e.width, e.height);
				gfx->ResizeBackbuffer(e.width, e.height);
				renderer->OnResize(e.width, e.height);
			});
		event_queue.Subscribe<ScrollEvent>([this](ScrollEvent const& e)
			{
				camera_manager.OnScroll(e.scroll);
			});
	}

	Engine::~Engine()
	{
		TaskSystem::Destroy();
		Log::Destroy();
	}

	void Engine::HandleWindowMessage(window_message_t const& msg_data)
	{
		input.HandleWindowMessage(msg_data);
	}

	void Engine::Run(RendererSettings const& settings, bool offscreen)
	{
		
		static EngineTimer timer;

		f32 const dt = timer.MarkInSeconds();

		if (Window::IsActive())
		{
			input.NewFrame();
			event_queue.ProcessEvents();

			Update(dt);
			Render(settings, offscreen);
		}
		else
		{
			input.NewFrame();
			event_queue.ProcessEvents();
		}

	}

	void Engine::Update(f32 dt)
	{
		camera_manager.Update(dt);
		auto& camera = camera_manager.GetActiveCamera();
		renderer->NewFrame(&camera);
		renderer->Update(dt);
	}

	void Engine::Render(RendererSettings const& settings, bool offscreen)
	{
		renderer->Render(settings);

		if (offscreen) renderer->ResolveToOffscreenFramebuffer();
		else
		{
			gfx->ClearBackbuffer();
			renderer->ResolveToBackbuffer();
		}
	}

	void Engine::Present()
	{
		gfx->SwapBuffers(vsync);
	}

	void Engine::InitializeScene(bool load_sponza) 
	{

		skybox_parameters_t skybox_params{};
		skybox_params.cubemap = L"Resources/Textures/Skybox/monoLake.hdr"; 

		entity_loader->LoadSkybox(skybox_params);

		if (load_sponza)
		{
			model_parameters_t model_params{};
			model_params.model_path = "Resources/GLTF Models/Sponza/glTF/Sponza.gltf";
			model_params.textures_path = "Resources/GLTF Models/Sponza/glTF/";
			model_params.model = DirectX::XMMatrixScaling(0.3f, 0.3f, 0.3f);
			entity_loader->LoadGLTFModel(model_params);
		}


		//terrain test
		{
			terrain_parameters_t terrain_params{};
			terrain_params.terrain_grid.tile_count_x = 100;
			terrain_params.terrain_grid.tile_count_z = 100;
			terrain_params.terrain_grid.normal_type = NormalCalculation::eEqualWeight;
			terrain_params.terrain_grid.tile_size_x = 1.0f;
			terrain_params.terrain_grid.tile_size_z = 1.0f;
			terrain_params.terrain_grid.texture_scale_x = 25;
			terrain_params.terrain_grid.texture_scale_z = 25;
			terrain_params.terrain_grid.split_to_chunks = false; //change this later

			noise_desc_t noise_desc{};
			noise_desc.width = 101;
			noise_desc.depth = 101;
			noise_desc.max_height = 25;
			terrain_params.terrain_grid.heightmap = std::make_unique<Heightmap>(noise_desc);
			terrain_params.terrain_texture_1 = "Resources/Textures/Random/grass.dds";
			entity_loader->LoadTerrain(terrain_params);
		}


		light_parameters_t light_params{};
		light_params.light_data.casts_shadows = true;
		light_params.light_data.color = DirectX::XMVectorSet(1.0f, 0.9f, 0.99f, 1.0f);
		light_params.light_data.energy = 8;
		light_params.light_data.direction = DirectX::XMVectorSet(0.1f, -1.0f, 0.25f, 0.0f);
		light_params.light_data.type = LightType::eDirectional;
		light_params.light_data.active = true;
		light_params.light_data.use_cascades = true;
		light_params.light_data.volumetric = false;
		light_params.light_data.volumetric_strength = 1.0f;
		light_params.mesh_type = LightMesh::eQuad;
		light_params.mesh_size = 150;
		
		entity_loader->LoadLight(light_params);

	}
}