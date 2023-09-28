#include "Editor.h"
#include <nfd.h>
#include "Rendering/Renderer.h"
#include "Graphics/GfxDevice.h"
#include "Rendering/ModelImporter.h"
#include "Logging/Logger.h"
#include "Utilities/FilesUtil.h"
#include "Utilities/StringUtil.h"
#include "Utilities/Random.h"

using namespace DirectX;

namespace adria
{
	using namespace tecs; 

	struct ProfilerState
	{
		bool  show_average;
		struct AccumulatedTimeStamp
		{
			float sum;
			float minimum;
			float maximum;

			AccumulatedTimeStamp()
				: sum(0.0f), minimum(FLT_MAX), maximum(0)
			{
			}
		};

		std::vector<AccumulatedTimeStamp> displayed_timestamps;
		std::vector<AccumulatedTimeStamp> accumulating_timestamps;
		double last_reset_time;
		uint32 accumulating_frame_count;
	};

	struct ImGuiLogger
	{
		ImGuiTextBuffer     Buf;
		ImGuiTextFilter     Filter;
		ImVector<int>       LineOffsets;
		bool                AutoScroll;

		ImGuiLogger()
		{
			AutoScroll = true;
			Clear();
		}

		void Clear()
		{
			Buf.clear();
			LineOffsets.clear();
			LineOffsets.push_back(0);
		}

		void AddLog(const char* fmt, ...) IM_FMTARGS(2)
		{
			int old_size = Buf.size();
			va_list args;
			va_start(args, fmt);
			Buf.appendfv(fmt, args);
			va_end(args);
			for (int new_size = Buf.size(); old_size < new_size; old_size++)
				if (Buf[old_size] == '\n')
					LineOffsets.push_back(old_size + 1);
		}

		void Draw(const char* title, bool* p_open = NULL)
		{
			if (!ImGui::Begin(title, p_open))
			{
				ImGui::End();
				return;
			}

			if (ImGui::BeginPopup("Options"))
			{
				ImGui::Checkbox("Auto-scroll", &AutoScroll);
				ImGui::EndPopup();
			}

			if (ImGui::Button("Options"))
				ImGui::OpenPopup("Options");
			ImGui::SameLine();
			bool clear = ImGui::Button("Clear");
			ImGui::SameLine();
			bool copy = ImGui::Button("Copy");
			ImGui::SameLine();
			Filter.Draw("Filter", -100.0f);

			ImGui::Separator();
			ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

			if (clear)
				Clear();
			if (copy)
				ImGui::LogToClipboard();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			const char* buf = Buf.begin();
			const char* buf_end = Buf.end();
			if (Filter.IsActive())
			{
				for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
				{
					const char* line_start = buf + LineOffsets[line_no];
					const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
					if (Filter.PassFilter(line_start, line_end))
						ImGui::TextUnformatted(line_start, line_end);
				}
			}
			else
			{
				ImGuiListClipper clipper;
				clipper.Begin(LineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char* line_start = buf + LineOffsets[line_no];
						const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
				clipper.End();
			}
			ImGui::PopStyleVar();

			if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);

			ImGui::EndChild();
			ImGui::End();
		}
	};
    class EditorLogger : public ILogger
    {
    public:
        EditorLogger(ImGuiLogger* logger, ELogLevel logger_level = ELogLevel::LOG_DEBUG) : logger{ logger }, logger_level{ logger_level } {}

        virtual void Log(ELogLevel level, char const* entry, char const* file, uint32_t line) override
        {
			if (level < logger_level) return;
			std::string log_entry = GetLogTime() + LevelToString(level) + std::string(entry) + "\n";
            if (logger) logger->AddLog(log_entry.c_str());
        }
    private:
        ImGuiLogger* logger;
        ELogLevel logger_level;
    };

    Editor::Editor(EditorInit const& init) : engine(), editor_log(new ImGuiLogger{})
    {
        engine = std::make_unique<Engine>(init.engine_init);
        gui = std::make_unique<GUI>(engine->gfx.get());
        ADRIA_REGISTER_LOGGER(new EditorLogger(editor_log.get()));

        SetStyle();
    }

    Editor::~Editor()
    {
    }

	void Editor::HandleWindowMessage(WindowMessage const& msg_data)
    {
        engine->HandleWindowMessage(msg_data);
        gui->HandleWindowMessage(msg_data);
    }

    void Editor::Run()
    {
        HandleInput();
        
        if (gui->IsVisible())
        {
            engine->SetSceneViewportData(scene_viewport_data);
            engine->Run(renderer_settings);
            engine->gfx->ClearBackbuffer();
            engine->gfx->SetBackbuffer();
            gui->Begin();
            {
				ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
                MenuBar();
                ListEntities();
				AddEntities();
				Properties();
                TerrainSettings();
                OceanSettings();
                SkySettings();
                ParticleSettings();
				DecalSettings();
                Camera();
                Scene();
                RendererSettings();
                Log();
				ShaderHotReload();
                StatsAndProfiling();
            }
            gui->End();
            engine->Present();
        }
        else
        {
            engine->SetSceneViewportData(std::nullopt);
            engine->Run(renderer_settings);
            engine->Present();
        }
    }

    void Editor::SetStyle()
    {
		ImGuiStyle& style = ImGui::GetStyle();
		ImGui::StyleColorsDark(&style);

		style.FrameRounding = 0.0f;
		style.GrabRounding = 1.0f;
		style.WindowRounding = 0.0f;
		style.IndentSpacing = 10.0f;
		style.WindowPadding = ImVec2(5, 5);
		style.FramePadding = ImVec2(2, 2);
		style.WindowBorderSize = 1.00f;
		style.ChildBorderSize = 1.00f;
		style.PopupBorderSize = 1.00f;
		style.FrameBorderSize = 1.00f;
		style.ScrollbarSize = 20.0f;
		style.WindowMenuButtonPosition = ImGuiDir_Right;

		ImVec4* colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text] = ImVec4(0.85f, 0.87f, 0.91f, 0.88f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.49f, 0.50f, 0.53f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
		colors[ImGuiCol_Border] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.09f, 0.09f, 0.09f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.23f, 0.26f, 0.32f, 0.60f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.53f, 0.75f, 0.82f, 0.86f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.61f, 0.74f, 0.87f, 1.00f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.24f, 0.31f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.17f, 0.19f, 0.23f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.53f, 0.75f, 0.82f, 0.86f);
    }

    void Editor::HandleInput()
    {
		if (scene_focused && g_Input.IsKeyDown(KeyCode::I)) gui->ToggleVisibility();
		if (scene_focused && g_Input.IsKeyDown(KeyCode::G)) gizmo_enabled = !gizmo_enabled;
        if (gizmo_enabled && gui->IsVisible())
        {
            if ( g_Input.IsKeyDown(KeyCode::T)) gizmo_op = ImGuizmo::TRANSLATE;
            if ( g_Input.IsKeyDown(KeyCode::R)) gizmo_op = ImGuizmo::ROTATE;
            if ( g_Input.IsKeyDown(KeyCode::E)) gizmo_op = ImGuizmo::SCALE; //e because s is for camera movement and its close to wasd and tr
        }
        engine->camera->Enable(scene_focused);
    }

	void Editor::MenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
				if (ImGui::MenuItem("Load Model"))
				{
					nfdchar_t* file_path = NULL;
					const nfdchar_t* filter_list = "gltf";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::string model_path = file_path;

						ModelParameters params{};
						params.model_path = model_path;
						std::string texture_path = GetParentPath(model_path);
						if (!texture_path.empty()) texture_path.append("/");

						params.textures_path = texture_path;
						engine->model_importer->ImportModel_GLTF(params);
						free(file_path);
					}
				}
                ImGui::EndMenu();
            }
			if (ImGui::BeginMenu("Windows"))
			{
				if (ImGui::MenuItem("Profiler", 0, window_flags[Flag_Profiler]))			 window_flags[Flag_Profiler] = !window_flags[Flag_Profiler];
				if (ImGui::MenuItem("Log", 0, window_flags[Flag_Log]))						 window_flags[Flag_Log] = !window_flags[Flag_Log];
				if (ImGui::MenuItem("Camera", 0, window_flags[Flag_Camera]))				 window_flags[Flag_Camera] = !window_flags[Flag_Camera];
				if (ImGui::MenuItem("Entities", 0, window_flags[Flag_Entities]))			 window_flags[Flag_Entities] = !window_flags[Flag_Entities];
				if (ImGui::MenuItem("Hot Reload", 0, window_flags[Flag_HotReload]))			 window_flags[Flag_HotReload] = !window_flags[Flag_HotReload];
				if (ImGui::MenuItem("Renderer Settings", 0, window_flags[Flag_Renderer]))	 window_flags[Flag_Renderer] = !window_flags[Flag_Renderer];
				if (ImGui::MenuItem("Terrain", 0, window_flags[Flag_Terrain]))				 window_flags[Flag_Terrain] = !window_flags[Flag_Terrain];
				if (ImGui::MenuItem("Ocean", 0, window_flags[Flag_Ocean]))					 window_flags[Flag_Ocean] = !window_flags[Flag_Ocean];
				if (ImGui::MenuItem("Decals", 0, window_flags[Flag_Decal]))					 window_flags[Flag_Decal] = !window_flags[Flag_Decal];
				if (ImGui::MenuItem("Particles", 0, window_flags[Flag_Particles]))			 window_flags[Flag_Particles] = !window_flags[Flag_Particles];
				if (ImGui::MenuItem("Sky", 0, window_flags[Flag_Sky]))						 window_flags[Flag_Sky] = !window_flags[Flag_Sky];
				if (ImGui::MenuItem("Add Entities", 0, window_flags[Flag_AddEntities]))		 window_flags[Flag_AddEntities] = !window_flags[Flag_AddEntities];

				ImGui::EndMenu();
			}
            if (ImGui::BeginMenu("Help"))
            {
                ImGui::Text("Controls\n");
                ImGui::Text(
                    "Move Camera with W, A, S, D, Q and E. Use Mouse for Rotating Camera. Use Mouse Scroll for Zoom In/Out.\n"
                    "Press I to toggle between Cinema Mode and Editor Mode. (Scene Window has to be active) \n"
                    "Press G to toggle Gizmo. (Scene Window has to be active) \n"
                    "Press F5 to recompile changed shaders.\n"
                    "When Gizmo is enabled, use T, R and E to switch between Translation, Rotation and Scaling Mode.\n"
                    "Left Click on entity to select it. Left click again on selected entity to unselect it.\n"
                    "Right Click on empty area in Entities window to add entity. Right Click on selected entity to delete it.\n" 
                    "When placing decals, left click on focused Scene window to pick a point for a decal (it's used only for "
					"decals currently but that could change in the future)"
                );
                ImGui::Spacing();

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

	void Editor::TerrainSettings()
	{
		if (!window_flags[Flag_Terrain]) return;

		if (ImGui::Begin("Terrain", &window_flags[Flag_Terrain]))
		{
            if (ImGui::TreeNodeEx("Terrain Generation", 0))
            {
				static GridParameters terrain_params{};
				static int32 tile_count[2] = { 600, 600 };
				static float tile_size[2] = { 2.0f, 2.0f };
				static float texture_scale[2] = { 200.0f, 200.0f };
				static int32 chunk_count[2] = { 40, 40 };
				static bool split_to_chunks = false;

				ImGui::SliderInt2("Tile Count", tile_count, 32, 2048);
				ImGui::SliderFloat2("Tile Size", tile_size, 0.1f, 20.0f);
				ImGui::SliderFloat2("Texture Scale", texture_scale, 1.0f, 400.0f);

				ImGui::Checkbox("Split Mesh to Chunks", &split_to_chunks);
				if (split_to_chunks)
				{
					ImGui::SliderInt2("Chunk Count", chunk_count, 8, 64);
				}

				static float max_height = 300;
				static bool procedural_generation = true;
				ImGui::SliderFloat("Max Height", &max_height, 10.0f, 2000.0f);
				ImGui::Checkbox("Procedural Generation", &procedural_generation);

				static NoiseDesc noise_desc
				{
				 .fractal_type = FractalType::FBM,
				 .noise_type = NoiseType::Perlin,
				 .seed = 33,
				 .persistence = 0.666f,
				 .lacunarity = 2.0f,
				 .octaves = 4,
				 .noise_scale = 24
				};
				if (procedural_generation)
				{
					noise_desc.width = (uint32)tile_count[0] + 1;
					noise_desc.depth = (uint32)tile_count[1] + 1;
					noise_desc.max_height = (uint32)max_height;

					ImGui::SliderInt("Seed", &noise_desc.seed, 1, 1000000);
					ImGui::SliderInt("Octaves", &noise_desc.octaves, 1, 16);
					ImGui::SliderFloat("Persistence", &noise_desc.persistence, 0.0f, 1.0f);
					ImGui::SliderFloat("Lacunarity", &noise_desc.lacunarity, 0.1f, 8.0f);
					ImGui::SliderFloat("Frequency", &noise_desc.frequency, 0.01f, 0.2f);
					ImGui::SliderFloat("Noise Scale", &noise_desc.noise_scale, 1, 128);

					const char* noise_types[] = { "OpenSimplex2", "OpenSimplex2S", "Cellular", "Perlin", "ValueCubic", "Value" };
					static int current_noise_type = 3;
					const char* noise_combo_label = noise_types[current_noise_type];
					if (ImGui::BeginCombo("Noise Type", noise_combo_label, 0))
					{
						for (int n = 0; n < IM_ARRAYSIZE(noise_types); n++)
						{
							const bool is_selected = (current_noise_type == n);
							if (ImGui::Selectable(noise_types[n], is_selected)) current_noise_type = n;
							if (is_selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					noise_desc.noise_type = static_cast<NoiseType>(current_noise_type);

					const char* fractal_types[] = { "None", "FBM", "Ridged", "PingPong" };
					static int current_fractal_type = 1;
					const char* fractal_combo_label = fractal_types[current_fractal_type];
					if (ImGui::BeginCombo("Fractal Type", fractal_combo_label, 0))
					{
						for (int n = 0; n < IM_ARRAYSIZE(fractal_types); n++)
						{
							const bool is_selected = (current_fractal_type == n);
							if (ImGui::Selectable(fractal_types[n], is_selected)) current_fractal_type = n;
							if (is_selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					noise_desc.fractal_type = static_cast<FractalType>(current_fractal_type);
				}
				else
				{
					//todo
					//add heightmap and splat map texture choice
				}

                static bool thermal_erosion = false;
                static ThermalErosionDesc thermal_erosion_desc{.iterations = 3, .c = 0.5f, .talus = 0.025f};
				ImGui::Checkbox("Thermal Erosion", &thermal_erosion);
                if (thermal_erosion && ImGui::TreeNodeEx("Thermal Erosion Settings", 0))
                {
					ImGui::SliderInt("Iterations", &thermal_erosion_desc.iterations, 1, 10);
                    ImGui::SliderFloat("C", &thermal_erosion_desc.c, 0.01f, 1.0f);
                    ImGui::SliderFloat("Talus", &thermal_erosion_desc.talus, 4.0f / std::max(tile_count[0], tile_count[1]), 16.0f / std::max(tile_count[0], tile_count[1]));

					ImGui::TreePop();
					ImGui::Separator();
                }

				static bool hydraulic_erosion = false;
				static HydraulicErosionDesc hydraulic_erosion_desc{ .iterations = 3, .drops = 1000000, .carrying_capacity = 1.5f, .deposition_speed = 0.03f };
				ImGui::Checkbox("Hydraulic Erosion", &hydraulic_erosion);
				if (hydraulic_erosion && ImGui::TreeNodeEx("Hydraulic Erosion Settings", 0))
				{
					ImGui::SliderInt("Iterations", &hydraulic_erosion_desc.iterations, 1, 10);
					ImGui::SliderInt("Raindrops", &hydraulic_erosion_desc.drops, 10000, 5000000);
					ImGui::SliderFloat("Carrying capacity", &hydraulic_erosion_desc.carrying_capacity, 0.5f, 2.0f);
					ImGui::SliderFloat("Deposition Speed", &hydraulic_erosion_desc.deposition_speed, 0.01f, 0.1f);

					ImGui::TreePop();
					ImGui::Separator();
				}

                static TerrainTextureLayerParameters layer_params{};
				if (ImGui::TreeNodeEx("Layer Texture Settings", 0))
				{
					ImGui::SliderFloat("Sand Start", &layer_params.terrain_sand_start, -1000.0f, 10.0f);
					ImGui::SliderFloat("Sand End", &layer_params.terrain_sand_end, layer_params.terrain_sand_start, 25.0f);
                    ImGui::SliderFloat("Grass Start", &layer_params.terrain_grass_start, 0.0f, 100.0f);
                    ImGui::SliderFloat("Grass End", &layer_params.terrain_grass_end, layer_params.terrain_grass_start, 1000.0f);

                    ImGui::SliderFloat("Rock Start", &layer_params.terrain_rocks_start, 0.0f, 100.0f);
                    ImGui::SliderFloat("Slope Grass Start", &layer_params.terrain_slope_grass_start, 0.0f, 1.0f);
                    ImGui::SliderFloat("Slope Rocks Start", &layer_params.terrain_slope_rocks_start, 0.0f, 1.0f);

                    ImGui::SliderFloat("Terrain Height Mix Zone", &layer_params.height_mix_zone, 0.0f, 100.0f);
                    ImGui::SliderFloat("Terrain Slope Mix Zone", &layer_params.slope_mix_zone, 0.0f, 0.1f);

					ImGui::TreePop();
					ImGui::Separator();
				}

				if (ImGui::Button("Generate Terrain"))
				{
					engine->reg.destroy<TerrainComponent>();

					TerrainParameters params{};
					terrain_params.tile_size_x = tile_size[0];
					terrain_params.tile_size_z = tile_size[1];
					terrain_params.tile_count_x = tile_count[0];
					terrain_params.tile_count_z = tile_count[1];
					terrain_params.texture_scale_x = texture_scale[0];
					terrain_params.texture_scale_z = texture_scale[1];
					terrain_params.split_to_chunks = split_to_chunks;
					terrain_params.chunk_count_x = chunk_count[0];
					terrain_params.chunk_count_z = chunk_count[1];
					terrain_params.normal_type = ENormalCalculation::AreaWeight;
					terrain_params.heightmap = std::make_unique<Heightmap>(noise_desc);
                    if (thermal_erosion) terrain_params.heightmap->ApplyThermalErosion(thermal_erosion_desc);
                    if (hydraulic_erosion) terrain_params.heightmap->ApplyHydraulicErosion(hydraulic_erosion_desc);

					params.terrain_grid = std::move(terrain_params);
                    params.layer_params = layer_params;
					params.grass_texture = "Resources/Textures/Terrain/terrain_grass.dds";
					params.rock_texture = "Resources/Textures/Terrain/grass2.dds";
					params.base_texture = "Resources/Textures/Terrain/terrain_grass.dds";
					params.sand_texture = "Resources/Textures/Terrain/mud.jpg";
					static const char* layer_texture_name = "layer.tga";
					params.layer_texture = layer_texture_name;

					engine->model_importer->LoadTerrain(params);
				}

				ImGui::TreePop();
				ImGui::Separator();
            }

            if (ImGui::TreeNodeEx("Foliage Generation", 0))
            {
				static std::vector<FoliageParameters> foliages{};
				static FoliageParameters foliage_params{
					.foliage_count = 3000,
					.foliage_scale = 10,
                    .foliage_height_start = 0.0f,
					.foliage_height_end = 1000.0f,
					.foliage_slope_start = 0.95f };
				if (ImGui::TreeNode("Foliage Settings"))
				{
					ImGui::SliderInt("Foliage Count", &foliage_params.foliage_count, 100, 25000);
					ImGui::SliderFloat("Foliage Scale", &foliage_params.foliage_scale, 1.0f, 100.0f);
					ImGui::SliderFloat("Foliage Slope Start", &foliage_params.foliage_slope_start, 0.0f, 1.0f);
					ImGui::SliderFloat("Foliage Height Start", &foliage_params.foliage_height_start, -50.0f, 50.0f);
					ImGui::SliderFloat("Foliage Height End", &foliage_params.foliage_height_end, -100.0f, 1000.0f);

					const char* foliage_types[] = { "Single Quad", "Double Quad", "Triple Quad" };
					static int current_foliage_type = 0;
					const char* foliage_combo_label = foliage_types[current_foliage_type];
					if (ImGui::BeginCombo("Foliage Type", foliage_combo_label, 0))
					{
						for (int n = 0; n < IM_ARRAYSIZE(foliage_types); n++)
						{
							const bool is_selected = (current_foliage_type == n);
							if (ImGui::Selectable(foliage_types[n], is_selected)) current_foliage_type = n;
							if (is_selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					foliage_params.mesh_texture_pair.first = static_cast<FoliageMesh>(current_foliage_type);

					if (ImGui::Button("Add Foliage"))
					{
						static std::string foliage_textures[] = {
							"Resources/Textures/Foliage/foliage.png",
                            "Resources/Textures/Foliage/foliage2.png",
							"Resources/Textures/Foliage/foliage3.png",
							"Resources/Textures/Foliage/foliage4.png",
							"Resources/Textures/Foliage/grass_flower_type1.png",
							"Resources/Textures/Foliage/grass_flower_type3.png",
							"Resources/Textures/Foliage/grass_flower_type10.png",
							"Resources/Textures/Foliage/grass_type1.png",
							"Resources/Textures/Foliage/grass_type2.png",
							"Resources/Textures/Foliage/grass_type3.png",
							"Resources/Textures/Foliage/grass_type4.png",
							"Resources/Textures/Foliage/grass_type6.png",
						};
						static IntRandomGenerator<size_t> index(0ll, ARRAYSIZE(foliage_textures) - 1);

						foliage_params.mesh_texture_pair.second = foliage_textures[index()];
						foliages.push_back(foliage_params);
					}

					ImGui::TreePop();
					ImGui::Separator();
				}
				ImGui::Text("Number of foliages to add: %d", foliages.size());
				if (ImGui::Button("Generate Foliage"))
				{
					if (TerrainComponent::terrain != nullptr)
					{
						auto [tile_size_x, tile_size_z] = TerrainComponent::terrain->TileSizes();
						auto [tile_count_x, tile_count_z] = TerrainComponent::terrain->TileCounts();
						for (auto& foliage_params : foliages)
						{
							foliage_params.foliage_center.x = tile_size_x * tile_count_x / 2;
							foliage_params.foliage_center.y = tile_size_z * tile_count_z / 2;
							foliage_params.foliage_extents.x = tile_size_x * tile_count_x / 2;
							foliage_params.foliage_extents.y = tile_size_z * tile_count_z / 2;

							engine->model_importer->LoadFoliage(foliage_params);
						}

						foliages.clear();
					}
				}

				ImGui::TreePop();
				ImGui::Separator();
            }

			if (ImGui::TreeNodeEx("Tree Generation", 0))
			{
				static std::vector<TreeParameters> trees{};
				static TreeParameters tree_params{
					.tree_count = 5,
					.tree_scale = 50,
					.tree_height_start = 0.0f,
					.tree_height_end = 1000.0f,
					.tree_slope_start = 0.95f };
				if (ImGui::TreeNode("Tree Settings"))
				{
					ImGui::SliderInt("Tree Count", &tree_params.tree_count, 1, 50);
					ImGui::SliderFloat("Tree Scale", &tree_params.tree_scale, 10.0f, 100.0f);
					ImGui::SliderFloat("Tree Slope Start", &tree_params.tree_slope_start, 0.0f, 1.0f);
					ImGui::SliderFloat("Tree Height Start", &tree_params.tree_height_start, 10.0f, 200.0f);
					ImGui::SliderFloat("Tree Height End", &tree_params.tree_height_end, 200.0f, 1000.0f);

					const char* tree_types[] = { "Tree01", "Tree02"};
					static int current_tree_type = 0;
					const char* tree_combo_label = tree_types[current_tree_type];
					if (ImGui::BeginCombo("Tree Type", tree_combo_label, 0))
					{
						for (int n = 0; n < IM_ARRAYSIZE(tree_types); n++)
						{
							const bool is_selected = (current_tree_type == n);
							if (ImGui::Selectable(tree_types[n], is_selected)) current_tree_type = n;
							if (is_selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					tree_params.tree_type = static_cast<TreeType>(current_tree_type);

					if (ImGui::Button("Add Trees"))
					{
                        trees.push_back(tree_params);
					}

					ImGui::TreePop();
					ImGui::Separator();
				}
				ImGui::Text("Number of trees to add: %d", trees.size());
				if (ImGui::Button("Generate Trees"))
				{
					if (TerrainComponent::terrain != nullptr)
					{
						auto [tile_size_x, tile_size_z] = TerrainComponent::terrain->TileSizes();
						auto [tile_count_x, tile_count_z] = TerrainComponent::terrain->TileCounts();
						for (auto& tree_params : trees)
						{
							tree_params.tree_center.x = tile_size_x * tile_count_x / 2;
							tree_params.tree_center.y = tile_size_z * tile_count_z / 2;
							tree_params.tree_extents.x = tile_size_x * tile_count_x / 2;
							tree_params.tree_extents.y = tile_size_z * tile_count_z / 2;

							engine->model_importer->LoadTrees(tree_params);
						}

                        trees.clear();
					}
				}

				ImGui::TreePop();
				ImGui::Separator();
			}


			if (ImGui::Button("Clear"))
			{
				engine->reg.destroy<TerrainComponent>();
                TerrainComponent::terrain = nullptr;
			}
			if (ImGui::TreeNodeEx("Terrain Settings", 0))
			{
				ImGui::TreePop();
				ImGui::Separator();
			}

		}
		ImGui::End();
	}

	void Editor::OceanSettings()
    {
		if (!window_flags[Flag_Ocean]) return;

		if (ImGui::Begin("Ocean", &window_flags[Flag_Ocean]))
        {
			static GridParameters ocean_params{};
			static int32 tile_count[2] = { 512, 512 };
			static float tile_size[2] = { 40.0f, 40.0f };
			static float texture_scale[2] = { 20.0f, 20.0f };

			ImGui::SliderInt2("Tile Count", tile_count, 32, 1024);
			ImGui::SliderFloat2("Tile Size", tile_size, 1.0, 100.0f);
			ImGui::SliderFloat2("Texture Scale", texture_scale, 0.1f, 10.0f);

			ocean_params.tile_count_x = tile_count[0];
			ocean_params.tile_count_z = tile_count[1];
			ocean_params.tile_size_x = tile_size[0];
			ocean_params.tile_size_z = tile_size[1];
			ocean_params.texture_scale_x = texture_scale[0];
			ocean_params.texture_scale_z = texture_scale[1];

			if (ImGui::Button("Load Ocean"))
			{
				OceanParameters params{};
				params.ocean_grid = std::move(ocean_params);
				engine->model_importer->LoadOcean(params);
			}

			if (ImGui::Button("Clear"))
			{
				engine->reg.destroy<Ocean>();
			}

			if (ImGui::TreeNodeEx("Ocean Settings", 0))
			{
				ImGui::Checkbox("Tessellation", &renderer_settings.ocean_tesselation);
				ImGui::Checkbox("Wireframe", &renderer_settings.ocean_wireframe);

				ImGui::SliderFloat("Choppiness", &renderer_settings.ocean_choppiness, 0.0f, 10.0f);
				renderer_settings.ocean_color_changed = ImGui::ColorEdit3("Ocean Color", renderer_settings.ocean_color);
				ImGui::TreePop();
				ImGui::Separator();
			}
        }
        ImGui::End();
    }

	void Editor::SkySettings()
	{
		if (!window_flags[Flag_Sky]) return;
		if (ImGui::Begin("Sky", &window_flags[Flag_Sky]))
        {
			const char* sky_types[] = { "Skybox", "Uniform Color", "Hosek-Wilkie" };
			static int current_sky_type = 0;
			const char* combo_label = sky_types[current_sky_type];
			if (ImGui::BeginCombo("Sky Type", combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(sky_types); n++)
				{
					const bool is_selected = (current_sky_type == n);
					if (ImGui::Selectable(sky_types[n], is_selected)) current_sky_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			if (current_sky_type == 0) renderer_settings.sky_type = SkyType::Skybox;
			else if (current_sky_type == 1)
			{
				renderer_settings.sky_type = SkyType::UniformColor;
				static char const* const sky_colors[] = { "Deep Sky Blue", "Sky Blue", "Light Sky Blue" };
				static int current_sky_color = 0;
				ImGui::ListBox("Tone Map Operator", &current_sky_color, sky_colors, IM_ARRAYSIZE(sky_colors));

				switch (current_sky_color)
				{
				case 0:
				{
					static float deep_sky_blue[3] = { 0.0f, 0.75f, 1.0f };
					memcpy(renderer_settings.sky_color, deep_sky_blue, sizeof(deep_sky_blue));
					break;
				}
				case 1:
				{
					static float sky_blue[3] = { 0.53f, 0.81f, 0.92f };
					memcpy(renderer_settings.sky_color, sky_blue, sizeof(sky_blue));
					break;
				}
				case 2:
				{
					static float light_sky_blue[3] = { 0.53f, 0.81f, 0.98f };
					memcpy(renderer_settings.sky_color, light_sky_blue, sizeof(light_sky_blue));
					break;
				}
				default:
					ADRIA_ASSERT(false);
				}
			}
			else if (current_sky_type == 2)
			{
				renderer_settings.sky_type = SkyType::HosekWilkie;
				ImGui::SliderFloat("Turbidity", &renderer_settings.turbidity, 2.0f, 30.0f);
				ImGui::SliderFloat("Ground Albedo", &renderer_settings.ground_albedo, 0.0f, 1.0f);
			}

        }
        ImGui::End();
	}

	void Editor::ParticleSettings()
	{
		if (!window_flags[Flag_Particles]) return;
		if (ImGui::Begin("Particles", &window_flags[Flag_Particles]))
        {
            static EmitterParameters params{};
            static char NAME_BUFFER[128];
            ImGui::InputText("Name", NAME_BUFFER, sizeof(NAME_BUFFER));
            params.name = std::string(NAME_BUFFER);
			if (ImGui::Button("Select Texture"))
			{
				nfdchar_t* file_path = NULL;
				nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
				nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
				if (result == NFD_OKAY)
				{
					std::string texture_path(file_path);
					params.texture_path = texture_path;
					free(file_path);
				}
			}
            ImGui::Text(params.texture_path.c_str());

			ImGui::SliderFloat3("Position", params.position, -500.0f, 500.0f);
			ImGui::SliderFloat3("Velocity", params.velocity, -50.0f, 50.0f);
			ImGui::SliderFloat3("Position Variance", params.position_variance, -50.0f, 50.0f);
			ImGui::SliderFloat("Velocity Variance", &params.velocity_variance, -10.0f, 10.0f);
			ImGui::SliderFloat("Lifespan", &params.lifespan, 0.0f, 50.0f);
			ImGui::SliderFloat("Start Size", &params.start_size, 0.0f, 50.0f);
			ImGui::SliderFloat("End Size", &params.end_size, 0.0f, 10.0f);
			ImGui::SliderFloat("Mass", &params.mass, 0.0f, 10.0f);
			ImGui::SliderFloat("Particles Per Second", &params.particles_per_second, 1.0f, 1000.0f);
			ImGui::Checkbox("Alpha Blend", &params.blend);
			ImGui::Checkbox("Collisions", &params.collisions);
			ImGui::Checkbox("Sort", &params.sort);
            if (params.collisions) ImGui::SliderInt("Collision Thickness", &params.collision_thickness, 0, 40);

			if (ImGui::Button("Load Emitter"))
			{
				engine->model_importer->LoadEmitter(params);
			}
			if (ImGui::Button("Clear Particle Emitters"))
			{
				engine->reg.destroy<Emitter>();
			}
        }
		ImGui::End();
	}

	void Editor::DecalSettings()
	{
		if (!window_flags[Flag_Decal]) return;
		if (ImGui::Begin("Decals", &window_flags[Flag_Decal]))
		{
			static DecalParameters params{};
			static char NAME_BUFFER[128];
			ImGui::InputText("Name", NAME_BUFFER, sizeof(NAME_BUFFER));
			params.name = std::string(NAME_BUFFER);
			ImGui::PushID(6);
			if (ImGui::Button("Select Albedo Texture"))
			{
				nfdchar_t* file_path = NULL;
				nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
				nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
				if (result == NFD_OKAY)
				{
					std::string texture_path = file_path;
					params.albedo_texture_path = texture_path;
					free(file_path);
				}
			}
			ImGui::PopID();
			ImGui::Text(params.albedo_texture_path.c_str());

			ImGui::PushID(7);
			if (ImGui::Button("Select Normal Texture"))
			{
				nfdchar_t* file_path = NULL;
				nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
				nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
				if (result == NFD_OKAY)
				{
					std::string texture_path = file_path;
					params.normal_texture_path = texture_path;
					free(file_path);
				}
			}

			ImGui::PopID();
			ImGui::Text(params.normal_texture_path.c_str());

			ImGui::DragFloat("Size", &params.size, 2.0f, 10.0f, 200.0f);
			ImGui::DragFloat("Rotation", &params.rotation, 1.0f, -180.0f, 180.0f);
			ImGui::Checkbox("Modify GBuffer Normals", &params.modify_gbuffer_normals);

			auto picking_data = engine->renderer->GetLastPickingData();
			ImGui::Text("Picked Position: %f %f %f", picking_data.position.x, picking_data.position.y, picking_data.position.z);
			ImGui::Text("Picked Normal: %f %f %f", picking_data.normal.x, picking_data.normal.y, picking_data.normal.z);
			if (ImGui::Button("Load Decal"))
			{
				params.position = Vector3(picking_data.position);
				params.normal = Vector3(picking_data.normal);
				params.rotation = XMConvertToRadians(params.rotation);

				engine->model_importer->LoadDecal(params);
			}
			if (ImGui::Button("Clear Decals"))
			{
				engine->reg.destroy<Decal>();
			}
		}
		ImGui::End();
	}

	void Editor::ListEntities()
    {
		if (!window_flags[Flag_Entities]) return;
        auto all_entities = engine->reg.view<Tag>();
		if (ImGui::Begin("Entities", &window_flags[Flag_Entities]))
        {
			std::function<void(entity, bool)> ShowEntity;
			ShowEntity = [&](entity e, bool first_iteration)
			{
				Relationship* relationship = engine->reg.get_if<Relationship>(e);
				if (first_iteration && relationship && relationship->parent != null_entity) return;
				auto& tag = all_entities.get(e);

				ImGuiTreeNodeFlags flags = ((selected_entity == e) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
				flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
				bool opened = ImGui::TreeNodeEx(tag.name.c_str(), flags);

				if (ImGui::IsItemClicked())
				{
					if (e == selected_entity) selected_entity = null_entity;
					else selected_entity = e;
				}

				if (opened)
				{
					if (relationship)
					{
						for (size_t i = 0; i < relationship->children_count; ++i)
						{
							ShowEntity(relationship->children[i], false);
						}
					}
					ImGui::TreePop();
				}
			};
			for (auto e : all_entities) ShowEntity(e, true);
        }
        ImGui::End();
    }

	void Editor::AddEntities()
	{
		if (!window_flags[Flag_AddEntities]) return;
		if (ImGui::Begin("Add Entities", &window_flags[Flag_AddEntities]))
		{
			ImGui::Text("For Easy Demonstration of Tiled/Clustered Deferred Rendering");
			static int light_count_to_add = 1;
			ImGui::SliderInt("Light Count", &light_count_to_add, 1, 128);
			if (ImGui::Button("Create Random Lights"))
			{
				static RealRandomGenerator real(0.0f, 1.0f);

				for (int32 i = 0; i < light_count_to_add; ++i)
				{
					LightParameters light_params{};
					light_params.light_data.casts_shadows = false;
					light_params.light_data.color = Vector4(real() * 2, real() * 2, real() * 2, 1.0f);
					light_params.light_data.direction = Vector4(0.5f, -1.0f, 0.1f, 0.0f);
					light_params.light_data.position = Vector4(real() * 200 - 100, real() * 200.0f, real() * 200 - 100, 1.0f);
					light_params.light_data.type = LightType::Point;
					light_params.mesh_type = LightMesh::NoMesh;
					light_params.light_data.range = real() * 100.0f + 40.0f;
					light_params.light_data.active = true;
					light_params.light_data.volumetric = false;
					light_params.light_data.volumetric_strength = 1.0f;
					engine->model_importer->LoadLight(light_params);
				}
			}
		}
		ImGui::End();
	}

	void Editor::Properties()
    {
		if (!window_flags[Flag_Entities]) return;
		if (ImGui::Begin("Properties", &window_flags[Flag_Entities]))
        {
            if (selected_entity != null_entity)
            {
                auto tag = engine->reg.get_if<Tag>(selected_entity);
                if (tag)
                {
                    char buffer[256];
                    memset(buffer, 0, sizeof(buffer));
                    std::strncpy(buffer, tag->name.c_str(), sizeof(buffer));
                    if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
                        tag->name = std::string(buffer);
                }

                auto light = engine->reg.get_if<Light>(selected_entity);
                if (light && ImGui::CollapsingHeader("Light"))
                {

					if (light->type == LightType::Directional)			ImGui::Text("Directional Light");
					else if (light->type == LightType::Spot)			ImGui::Text("Spot Light");
					else if (light->type == LightType::Point)			ImGui::Text("Point Light");

					Vector4 light_color = light->color, light_direction = light->direction, light_position = light->position;
					float color[3] = { light_color.x, light_color.y, light_color.z };
					ImGui::ColorEdit3("Light Color", color);

					light->color = Vector4(color[0], color[1], color[2], 1.0f);

					ImGui::SliderFloat("Light Energy", &light->energy, 0.0f, 50.0f);

					if (engine->reg.has<Material>(selected_entity))
					{
						auto& material = engine->reg.get<Material>(selected_entity);
						material.diffuse = Vector3(color[0], color[1], color[2]);
					}

					if (light->type == LightType::Directional || light->type == LightType::Spot)
					{
						float direction[3] = { light_direction.x, light_direction.y, light_direction.z };

						ImGui::SliderFloat3("Light direction", direction, -1.0f, 1.0f);

						light->direction = Vector4(direction[0], direction[1], direction[2], 0.0f);

						if (light->type == LightType::Directional)
						{
							light->position = -light->direction * 1e3;
						}
					}

					if (light->type == LightType::Spot)
					{
						float inner_angle = XMConvertToDegrees(acos(light->inner_cosine))
							, outer_angle = XMConvertToDegrees(acos(light->outer_cosine));
						ImGui::SliderFloat("Inner Spot Angle", &inner_angle, 0.0f, 90.0f);
						ImGui::SliderFloat("Outer Spot Angle", &outer_angle, inner_angle, 90.0f);

						light->inner_cosine = cos(XMConvertToRadians(inner_angle));
						light->outer_cosine = cos(XMConvertToRadians(outer_angle));
					}

					if (light->type == LightType::Point || light->type == LightType::Spot)
					{
						float position[3] = { light_position.x, light_position.y, light_position.z };

						ImGui::SliderFloat3("Light position", position, -300.0f, 500.0f);

						light->position = Vector4(position[0], position[1], position[2], 1.0f);

						ImGui::SliderFloat("Range", &light->range, 50.0f, 1000.0f);
					}

					if (engine->reg.has<Transform>(selected_entity))
					{
						auto& tr = engine->reg.get<Transform>(selected_entity);

						tr.current_transform = XMMatrixTranslationFromVector(light->position);
					}

					ImGui::Checkbox("Active", &light->active);
					ImGui::Checkbox("God Rays", &light->god_rays);

					if (light->god_rays)
					{
						ImGui::SliderFloat("God Rays decay", &light->godrays_decay, 0.0f, 1.0f);
						ImGui::SliderFloat("God Rays weight", &light->godrays_weight, 0.0f, 0.5f);
						ImGui::SliderFloat("God Rays density", &light->godrays_density, 0.1f, 3.0f);
						ImGui::SliderFloat("God Rays exposure", &light->godrays_exposure, 0.1f, 10.0f);
					}

					ImGui::Checkbox("Casts Shadows", &light->casts_shadows);

					ImGui::Checkbox("Screen Space Contact Shadows", &light->screen_space_contact_shadows);
                    if (light->screen_space_contact_shadows)
                    {
                        ImGui::SliderFloat("Thickness", &light->sscs_thickness, 0.0f, 1.0f);
                        ImGui::SliderFloat("Max Ray Distance", &light->sscs_max_ray_distance, 0.0f, 0.3f);
                        ImGui::SliderFloat("Max Depth Distance", &light->sscs_max_depth_distance, 0.0f, 500.0f);
                    }

					ImGui::Checkbox("Volumetric Lighting", &light->volumetric);
					if (light->volumetric)
					{
						ImGui::SliderFloat("Volumetric lighting Strength", &light->volumetric_strength, 0.0f, 5.0f);
					}

					ImGui::Checkbox("Lens Flare", &light->lens_flare);

					if (light->type == LightType::Directional && light->casts_shadows)
					{
						bool use_cascades = static_cast<bool>(light->use_cascades);
						ImGui::Checkbox("Use Cascades", &use_cascades);
						light->use_cascades = use_cascades;
					}

                }

                auto material = engine->reg.get_if<Material>(selected_entity);
                if (material && ImGui::CollapsingHeader("Material"))
                {
					ImGui::Text("Albedo Texture");
					ImGui::Image(g_TextureManager.GetTextureDescriptor(material->albedo_texture), ImVec2(48.0f, 48.0f));

					ImGui::PushID(0);
					if (ImGui::Button("Remove")) material->albedo_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->albedo_texture = g_TextureManager.LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Metallic-Roughness Texture");
					ImGui::Image(g_TextureManager.GetTextureDescriptor(material->metallic_roughness_texture), ImVec2(48.0f, 48.0f));

					ImGui::PushID(1);
					if (ImGui::Button("Remove")) material->metallic_roughness_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->metallic_roughness_texture = g_TextureManager.LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Emissive Texture");
					ImGui::Image(g_TextureManager.GetTextureDescriptor(material->emissive_texture), ImVec2(48.0f, 48.0f));

					ImGui::PushID(2);
					if (ImGui::Button("Remove")) material->emissive_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->emissive_texture = g_TextureManager.LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::ColorEdit3("Albedo Color", &material->diffuse.x);
					ImGui::SliderFloat("Albedo Factor", &material->albedo_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Metallic Factor", &material->metallic_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Roughness Factor", &material->roughness_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Emissive Factor", &material->emissive_factor, 0.0f, 32.0f);

					//add shader changing
					if (engine->reg.has<Forward>(selected_entity))
					{
						if (material->albedo_texture != INVALID_TEXTURE_HANDLE)
							material->shader = ShaderProgram::Texture;
						else material->shader = ShaderProgram::Solid;
					}
					else
					{
						material->shader = ShaderProgram::GBufferPBR;
					}
                }

                auto transform = engine->reg.get_if<Transform>(selected_entity);
                if (transform && ImGui::CollapsingHeader("Transform"))
                {
					Matrix tr = transform->current_transform;
					
					float translation[3], rotation[3], scale[3];
					ImGuizmo::DecomposeMatrixToComponents(tr.m[0], translation, rotation, scale);
					ImGui::InputFloat3("Translation", translation);
					ImGui::InputFloat3("Rotation", rotation);
					ImGui::InputFloat3("Scale", scale);
					ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, tr.m[0]);

					if (Emitter* emitter = engine->reg.get_if<Emitter>(selected_entity))
					{
						emitter->position = Vector4(translation[0], translation[1], translation[2], 1.0f);
					}

					if (AABB* aabb = engine->reg.get_if<AABB>(selected_entity))
					{
						aabb->bounding_box.Transform(aabb->bounding_box, transform->current_transform.Invert());
						aabb->bounding_box.Transform(aabb->bounding_box, tr);
						aabb->UpdateBuffer(engine->gfx.get());
					}

					if (Relationship* relationship = engine->reg.get_if<Relationship>(selected_entity))
					{
						for (size_t i = 0; i < relationship->children_count; ++i)
						{
							entity child = relationship->children[i];
							if (AABB* aabb = engine->reg.get_if<AABB>(child))
							{
								aabb->bounding_box.Transform(aabb->bounding_box, transform->current_transform.Invert());
								aabb->bounding_box.Transform(aabb->bounding_box, tr);
								aabb->UpdateBuffer(engine->gfx.get());
							}
						}
					}
					transform->current_transform = tr;
                }

                auto skybox = engine->reg.get_if<Skybox>(selected_entity);
                if (skybox && ImGui::CollapsingHeader("Skybox"))
                {
					ImGui::Checkbox("Active", &skybox->active);
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							skybox->cubemap_texture = g_TextureManager.LoadTexture(texture_path);
							free(file_path);
						}
					}
                }

                auto emitter = engine->reg.get_if<Emitter>(selected_entity);
                if (emitter && ImGui::CollapsingHeader("Emitter"))
                {
					ImGui::Text("Particle Texture");
					ImGui::Image(g_TextureManager.GetTextureDescriptor(emitter->particle_texture), ImVec2(48.0f, 48.0f));

					ImGui::PushID(3);
					if (ImGui::Button("Remove")) emitter->particle_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							emitter->particle_texture = g_TextureManager.LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					float pos[3] = { emitter->position.x, emitter->position.y, emitter->position.z },
						vel[3] = { emitter->velocity.x, emitter->velocity.y, emitter->velocity.z },
						pos_var[3] = { emitter->position_variance.x, emitter->position_variance.y, emitter->position_variance.z };

					ImGui::SliderFloat3("Position", pos, -500.0f, 500.0f);
					ImGui::SliderFloat3("Velocity", vel, -50.0f, 50.0f);
					ImGui::SliderFloat3("Position Variance", pos_var, -50.0f, 50.0f);
					emitter->position = Vector4(pos[0], pos[1], pos[2], 1.0f);
					emitter->velocity = Vector4(vel[0], vel[1], vel[2], 1.0f);
					emitter->position_variance = Vector4(pos_var[0], pos_var[1], pos_var[2], 1.0f);

					if (transform)
					{
						Matrix tr = transform->current_transform;
						float translation[3], rotation[3], scale[3];
						ImGuizmo::DecomposeMatrixToComponents(tr.m[0], translation, rotation, scale);
						ImGuizmo::RecomposeMatrixFromComponents(pos, rotation, scale, tr.m[0]);
						transform->current_transform = tr;
					}

					ImGui::SliderFloat("Velocity Variance", &emitter->velocity_variance, -10.0f, 10.0f);
					ImGui::SliderFloat("Lifespan", &emitter->particle_lifespan, 0.0f, 50.0f);
					ImGui::SliderFloat("Start Size", &emitter->start_size, 0.0f, 50.0f);
					ImGui::SliderFloat("End Size", &emitter->end_size, 0.0f, 10.0f);
					ImGui::SliderFloat("Mass", &emitter->mass, 0.0f, 10.0f);
					ImGui::SliderFloat("Particles Per Second", &emitter->particles_per_second, 1.0f, 1000.0f);

					ImGui::Checkbox("Alpha Blend", &emitter->alpha_blended);
					ImGui::Checkbox("Collisions", &emitter->collisions_enabled);
					if (emitter->collisions_enabled) ImGui::SliderInt("Collision Thickness", &emitter->collision_thickness, 0, 40);

					ImGui::Checkbox("Sort", &emitter->sort);
					ImGui::Checkbox("Pause", &emitter->pause);
					if (ImGui::Button("Reset")) emitter->reset_emitter = true;
                }

				auto decal = engine->reg.get_if<Decal>(selected_entity);
				if (decal && ImGui::CollapsingHeader("Decal"))
				{
					ImGui::Text("Decal Albedo Texture");
					ImGui::Image(g_TextureManager.GetTextureDescriptor(decal->albedo_decal_texture), ImVec2(48.0f, 48.0f));

					ImGui::PushID(4);
					if (ImGui::Button("Remove")) decal->albedo_decal_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							decal->albedo_decal_texture = g_TextureManager.LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::PushID(5);
					if (ImGui::Button("Remove")) decal->normal_decal_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							decal->normal_decal_texture = g_TextureManager.LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();
					ImGui::Checkbox("Modify GBuffer Normals", &decal->modify_gbuffer_normals);
				}

				if (AABB* aabb = engine->reg.get_if<AABB>(selected_entity))
				{
					aabb->draw_aabb = true;
				}
            }
        }
        ImGui::End();
    }

    void Editor::Camera()
    {
		if (!window_flags[Flag_Camera]) return;
		auto& camera = *engine->camera;
		if (ImGui::Begin("Camera", &window_flags[Flag_Camera]))
		{
			Vector3 cam_pos = camera.Position();
			float pos[3] = { cam_pos.x, cam_pos.y, cam_pos.z };
			ImGui::SliderFloat3("Position", pos, 0.0f, 10000.0f);
			camera.SetPosition(Vector3(pos));
			float near_plane = camera.Near(), far_plane = camera.Far();
			float _fov = camera.Fov(), _ar = camera.AspectRatio();
			ImGui::SliderFloat("Near Plane", &near_plane, 0.0f, 2.0f);
			ImGui::SliderFloat("Far Plane", &far_plane, 10.0f, 5000.0f);
			ImGui::SliderFloat("FOV", &_fov, 0.01f, 1.5707f);
			camera.SetNearAndFar(near_plane, far_plane);
			camera.SetFov(_fov);
		}
		ImGui::End();
    }

    void Editor::Scene()
    {
        ImGui::Begin("Scene");
        {
			ImVec2 v_min = ImGui::GetWindowContentRegionMin();
			ImVec2 v_max = ImGui::GetWindowContentRegionMax();
			v_min.x += ImGui::GetWindowPos().x;
			v_min.y += ImGui::GetWindowPos().y;
			v_max.x += ImGui::GetWindowPos().x;
			v_max.y += ImGui::GetWindowPos().y;
            ImVec2 size(v_max.x - v_min.x, v_max.y - v_min.y);

            scene_focused = ImGui::IsWindowFocused();
            ImGui::Image(engine->renderer->GetOffscreenTexture()->SRV(), size);
            //ImGui::GetForegroundDrawList()->AddRect(v_min, v_max, IM_COL32(255, 0, 0, 255));

            ImVec2 mouse_pos = ImGui::GetMousePos();
            scene_viewport_data.mouse_position_x = mouse_pos.x;
            scene_viewport_data.mouse_position_y = mouse_pos.y;
            scene_viewport_data.scene_viewport_focused = scene_focused;
            scene_viewport_data.scene_viewport_pos_x = v_min.x;
            scene_viewport_data.scene_viewport_pos_y = v_min.y;
			scene_viewport_data.scene_viewport_size_x = size.x;
			scene_viewport_data.scene_viewport_size_y = size.y;
        }

        if (selected_entity != null_entity && engine->reg.has<Transform>(selected_entity) && gizmo_enabled)
        {
            ImGuizmo::SetDrawlist();

            ImVec2 window_size = ImGui::GetWindowSize();
            ImVec2 window_pos = ImGui::GetWindowPos();

            ImGuizmo::SetRect(window_pos.x, window_pos.y,
                window_size.x, window_size.y);

            auto& camera = *engine->camera;

			Matrix camera_view = camera.View();
			Matrix camera_proj = camera.Proj();

			Transform& entity_transform = engine->reg.get<Transform>(selected_entity);

			Matrix tr = entity_transform.current_transform;
            ImGuizmo::Manipulate(camera_view.m[0], camera_proj.m[0], gizmo_op, ImGuizmo::LOCAL, tr.m[0]);

            if (ImGuizmo::IsUsing())
            {
                AABB* aabb = engine->reg.get_if<AABB>(selected_entity);
                if (aabb)
                {
					aabb->bounding_box.Transform(aabb->bounding_box, entity_transform.current_transform.Invert());
					aabb->bounding_box.Transform(aabb->bounding_box, tr);
					aabb->UpdateBuffer(engine->gfx.get());
                }
               
				if (Relationship* relationship = engine->reg.get_if<Relationship>(selected_entity))
				{
					for (uint32 i = 0; i < relationship->children_count; ++i)
					{
						entity child = relationship->children[i];
						if (AABB* aabb = engine->reg.get_if<AABB>(child))
						{
							aabb->bounding_box.Transform(aabb->bounding_box, entity_transform.current_transform.Invert());
							aabb->bounding_box.Transform(aabb->bounding_box, tr);
							aabb->UpdateBuffer(engine->gfx.get());
						}
					}
				}
				entity_transform.current_transform = tr;
            }
        }

        ImGui::End();
    }

    void Editor::Log()
    {
		if (!window_flags[Flag_Log]) return;
		if (ImGui::Begin("Log", &window_flags[Flag_Log]))
        {
            editor_log->Draw("Log");
        }
        ImGui::End();
    }

    void Editor::RendererSettings()
    {  
		if (!window_flags[Flag_Renderer]) return;
		if (ImGui::Begin("Renderer Settings", &window_flags[Flag_Renderer]))
        {
            if (ImGui::TreeNode("Deferred Settings"))
            {
                const char* deferred_types[] = { "Regular", "Tiled", "Clustered" };
                static int current_deferred_type = 0;
                const char* combo_label = deferred_types[current_deferred_type];
                if (ImGui::BeginCombo("Deferred Type", combo_label, 0))
                {
                    for (int n = 0; n < IM_ARRAYSIZE(deferred_types); n++)
                    {
                        const bool is_selected = (current_deferred_type == n);
                        if (ImGui::Selectable(deferred_types[n], is_selected)) current_deferred_type = n;
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                renderer_settings.use_tiled_deferred = (current_deferred_type == 1);
                renderer_settings.use_clustered_deferred = (current_deferred_type == 2);

                if (renderer_settings.use_tiled_deferred && ImGui::TreeNodeEx("Tiled Deferred", ImGuiTreeNodeFlags_OpenOnDoubleClick))
                {
                    ImGui::Checkbox("Visualize Tiles", &renderer_settings.visualize_tiled);
                    if (renderer_settings.visualize_tiled) ImGui::SliderInt("Visualize Scale", &renderer_settings.visualize_max_lights, 1, 32);

                    ImGui::TreePop();
                    ImGui::Separator();
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Global Illumination"))
            {

                ImGui::Checkbox("Voxel Cone Tracing GI", &renderer_settings.voxel_gi);

                if (renderer_settings.voxel_gi)
                {
                    bool voxel_size_changed = ImGui::SliderFloat("Voxel Size", &renderer_settings.voxel_size, 0.125f, 8.0f);
                    ImGui::SliderInt("Cone Number", &renderer_settings.voxel_num_cones, 1, 6);
                    ImGui::SliderFloat("Ray Step", &renderer_settings.voxel_ray_step_distance, 0.5f, 5.0f);
                    ImGui::SliderFloat("Max Distance", &renderer_settings.voxel_max_distance, 1.0f, 50.0f);
                    ImGui::Checkbox("Second Bounce", &renderer_settings.voxel_second_bounce);
                    ImGui::Checkbox("Voxel GI Debug", &renderer_settings.voxel_debug);
                }
                renderer_settings.voxel_debug = renderer_settings.voxel_debug && renderer_settings.voxel_gi; //voxel debug cannot be true unless voxel_gi is true

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Postprocessing"))
            {
                {
                    const char* ao_types[] = { "None", "SSAO", "HBAO" };
                    static int current_ao_type = 0;
                    const char* combo_label = ao_types[current_ao_type];
                    if (ImGui::BeginCombo("Ambient Occlusion", combo_label, 0))
                    {
                        for (int n = 0; n < IM_ARRAYSIZE(ao_types); n++)
                        {
                            const bool is_selected = (current_ao_type == n);
                            if (ImGui::Selectable(ao_types[n], is_selected)) current_ao_type = n;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    renderer_settings.ambient_occlusion = static_cast<AmbientOcclusion>(current_ao_type);

                    if (renderer_settings.ambient_occlusion == AmbientOcclusion::SSAO && ImGui::TreeNodeEx("SSAO", ImGuiTreeNodeFlags_OpenOnDoubleClick))
                    {
                        ImGui::SliderFloat("Power", &renderer_settings.ssao_power, 1.0f, 16.0f);
                        ImGui::SliderFloat("Radius", &renderer_settings.ssao_radius, 0.5f, 4.0f);

                        ImGui::TreePop();
                        ImGui::Separator();
                    }
                    if (renderer_settings.ambient_occlusion == AmbientOcclusion::HBAO && ImGui::TreeNodeEx("HBAO", ImGuiTreeNodeFlags_OpenOnDoubleClick))
                    {
                        ImGui::SliderFloat("Power", &renderer_settings.hbao_power, 1.0f, 16.0f);
                        ImGui::SliderFloat("Radius", &renderer_settings.hbao_radius, 0.25f, 8.0f);

                        ImGui::TreePop();
                        ImGui::Separator();
                    }
                }

                ImGui::Checkbox("Volumetric Clouds", &renderer_settings.clouds);
                ImGui::Checkbox("SSR", &renderer_settings.ssr);
                ImGui::Checkbox("DoF", &renderer_settings.dof);
                ImGui::Checkbox("Bloom", &renderer_settings.bloom);
                ImGui::Checkbox("Motion Blur", &renderer_settings.motion_blur);
                ImGui::Checkbox("Fog", &renderer_settings.fog);
                if (ImGui::TreeNode("Anti-Aliasing"))
                {
                    static bool fxaa = false, taa = false;
                    ImGui::Checkbox("FXAA", &fxaa);
                    ImGui::Checkbox("TAA", &taa);
                    if (fxaa)
                    {
                        renderer_settings.anti_aliasing = static_cast<AntiAliasing>(renderer_settings.anti_aliasing | AntiAliasing_FXAA);
                    }
                    else
                    {
                        renderer_settings.anti_aliasing = static_cast<AntiAliasing>(renderer_settings.anti_aliasing & (~AntiAliasing_FXAA));
                    }
                    if (taa)
                    {
                        renderer_settings.anti_aliasing = static_cast<AntiAliasing>(renderer_settings.anti_aliasing | AntiAliasing_TAA);
                    }
                    else
                    {
                        renderer_settings.anti_aliasing = static_cast<AntiAliasing>(renderer_settings.anti_aliasing & (~AntiAliasing_TAA));
                    }
                    
                    ImGui::TreePop();
                }
                if (renderer_settings.clouds && ImGui::TreeNodeEx("Volumetric Clouds", 0))
                {
                    ImGui::SliderFloat("Sun light absorption", &renderer_settings.light_absorption, 0.0f, 0.015f);
                    ImGui::SliderFloat("Clouds bottom height", &renderer_settings.clouds_bottom_height, 1000.0f, 10000.0f);
                    ImGui::SliderFloat("Clouds top height", &renderer_settings.clouds_top_height, 10000.0f, 50000.0f);
                    ImGui::SliderFloat("Density", &renderer_settings.density_factor, 0.0f, 1.0f);
                    ImGui::SliderFloat("Crispiness", &renderer_settings.crispiness, 0.0f, 100.0f);
                    ImGui::SliderFloat("Curliness", &renderer_settings.curliness, 0.0f, 5.0f);
                    ImGui::SliderFloat("Coverage", &renderer_settings.coverage, 0.0f, 1.0f);
                    ImGui::SliderFloat("Cloud Type", &renderer_settings.cloud_type, 0.0f, 1.0f);
                    ImGui::TreePop();
                    ImGui::Separator();
                }
                if (renderer_settings.ssr && ImGui::TreeNodeEx("Screen-Space Reflections", 0))
                {
                    ImGui::SliderFloat("Ray Step", &renderer_settings.ssr_ray_step, 1.0f, 3.0f);
                    ImGui::SliderFloat("Ray Hit Threshold", &renderer_settings.ssr_ray_hit_threshold, 0.25f, 5.0f);

                    ImGui::TreePop();
                    ImGui::Separator();
                }
                if (renderer_settings.dof && ImGui::TreeNodeEx("Depth Of Field", 0))
                {

                    ImGui::SliderFloat("DoF Near Blur", &renderer_settings.dof_near_blur, 0.0f, 200.0f);
                    ImGui::SliderFloat("DoF Near", &renderer_settings.dof_near, renderer_settings.dof_near_blur, 500.0f);
                    ImGui::SliderFloat("DoF Far", &renderer_settings.dof_far, renderer_settings.dof_near, 1000.0f);
                    ImGui::SliderFloat("DoF Far Blur", &renderer_settings.dof_far_blur, renderer_settings.dof_far, 1500.0f);
                    ImGui::Checkbox("Bokeh", &renderer_settings.bokeh);

                    if (renderer_settings.bokeh)
                    {
                        static char const* const bokeh_types[] = { "HEXAGON", "OCTAGON", "CIRCLE", "CROSS" };
                        static int bokeh_type_i = static_cast<int>(renderer_settings.bokeh_type);
                        ImGui::ListBox("Bokeh Type", &bokeh_type_i, bokeh_types, IM_ARRAYSIZE(bokeh_types));
                        renderer_settings.bokeh_type = static_cast<BokehType>(bokeh_type_i);

                        ImGui::SliderFloat("Bokeh Blur Threshold", &renderer_settings.bokeh_blur_threshold, 0.0f, 1.0f);
                        ImGui::SliderFloat("Bokeh Lum Threshold", &renderer_settings.bokeh_lum_threshold, 0.0f, 10.0f);
                        ImGui::SliderFloat("Bokeh Color Scale", &renderer_settings.bokeh_color_scale, 0.1f, 10.0f);
                        ImGui::SliderFloat("Bokeh Max Size", &renderer_settings.bokeh_radius_scale, 0.0f, 100.0f);
                        ImGui::SliderFloat("Bokeh Fallout", &renderer_settings.bokeh_fallout, 0.0f, 2.0f);
                    }
                    ImGui::TreePop();
                    ImGui::Separator();

                }
                if (renderer_settings.bloom && ImGui::TreeNodeEx("Bloom", 0))
                {
                    ImGui::SliderFloat("Threshold", &renderer_settings.bloom_threshold, 0.1f, 2.0f);
                    ImGui::SliderFloat("Bloom Scale", &renderer_settings.bloom_scale, 0.1f, 5.0f);
                    ImGui::TreePop();
                    ImGui::Separator();
                }
                if ((renderer_settings.motion_blur || (renderer_settings.anti_aliasing & AntiAliasing_TAA)) && ImGui::TreeNodeEx("Velocity Buffer", 0))
                {
                    ImGui::SliderFloat("Motion Blur Scale", &renderer_settings.velocity_buffer_scale, 32.0f, 128.0f);
                    ImGui::TreePop();
                    ImGui::Separator();
                }
                if (renderer_settings.fog && ImGui::TreeNodeEx("Fog", 0))
                {
                    const char* fog_types[] = { "Exponential", "Exponential Height"};
                    static int current_fog_type = 0; 
                    const char* combo_label = fog_types[current_fog_type];  
                    if (ImGui::BeginCombo("Fog Type", combo_label, 0))
                    {
                        for (int n = 0; n < IM_ARRAYSIZE(fog_types); n++)
                        {
                            const bool is_selected = (current_fog_type == n);
                            if (ImGui::Selectable(fog_types[n], is_selected)) current_fog_type = n;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    
                    renderer_settings.fog_type = static_cast<FogType>(current_fog_type);

                    ImGui::SliderFloat("Fog Falloff", &renderer_settings.fog_falloff, 0.0001f, 0.01f);
                    ImGui::SliderFloat("Fog Density", &renderer_settings.fog_density, 0.0001f, 0.01f);
                    ImGui::SliderFloat("Fog Start", &renderer_settings.fog_start, 0.1f, 10000.0f);
                    ImGui::ColorEdit3("Fog Color", renderer_settings.fog_color);

                    ImGui::TreePop();
                    ImGui::Separator();
                }
                if (ImGui::TreeNodeEx("Tone Mapping", 0))
                {
                    ImGui::SliderFloat("Exposure", &renderer_settings.tone_map_exposure, 0.01f, 10.0f);
                    static char const* const operators[] = { "REINHARD", "HABLE", "LINEAR" };
                    static int tone_map_operator = static_cast<int>(renderer_settings.tone_map_op);
                    ImGui::ListBox("Tone Map Operator", &tone_map_operator, operators, IM_ARRAYSIZE(operators));
                    renderer_settings.tone_map_op = static_cast<ToneMap>(tone_map_operator);
                    ImGui::TreePop();
                    ImGui::Separator();
                }

                ImGui::TreePop();
            }
            
            if (ImGui::TreeNode("Misc"))
            {

				renderer_settings.recreate_initial_spectrum = ImGui::SliderFloat2("Wind Direction", renderer_settings.wind_direction, 0.0f, 50.0f);
				ImGui::SliderFloat("Wind speed factor", &renderer_settings.wind_speed, 0.0f, 100.0f);
                ImGui::ColorEdit3("Ambient Color", renderer_settings.ambient_color);
                ImGui::SliderFloat("Shadow Softness", &renderer_settings.shadow_softness, 0.01f, 5.0f);
                ImGui::Checkbox("Transparent Shadows", &renderer_settings.shadow_transparent);
                ImGui::Checkbox("IBL", &renderer_settings.ibl);

                //random lights
                {
                    ImGui::Text("For Easy Demonstration of Tiled/Clustered Deferred Rendering");
                    static int light_count_to_add = 1;
                    ImGui::SliderInt("Light Count", &light_count_to_add, 1, 128);

                    if (ImGui::Button("Random Point Lights"))
                    {
                        static RealRandomGenerator real(0.0f, 1.0f);

                        for (int32 i = 0; i < light_count_to_add; ++i)
                        {
                            LightParameters light_params{};
                            light_params.light_data.casts_shadows = false;
                            light_params.light_data.color = Vector4(real() * 2, real() * 2, real() * 2, 1.0f);
                            light_params.light_data.direction = Vector4(0.5f, -1.0f, 0.1f, 0.0f);
                            light_params.light_data.position = Vector4(real() * 500 - 250, real() * 500.0f, real() * 500 - 250, 1.0f);
                            light_params.light_data.type = LightType::Point;
                            light_params.mesh_type = LightMesh::NoMesh;
                            light_params.light_data.range = real() * 100.0f + 40.0f;
                            light_params.light_data.active = true;
                            light_params.light_data.volumetric = false;
                            light_params.light_data.volumetric_strength = 1.0f;
                            engine->model_importer->LoadLight(light_params);
                        }
                    }

                }

                ImGui::TreePop();
            }
        }
        ImGui::End();
    }

	void Editor::ShaderHotReload()
	{
		if (!window_flags[Flag_HotReload]) return;
		if (ImGui::Begin("Shader Hot Reload", &window_flags[Flag_HotReload]))
		{
			if (ImGui::Button("Compile Changed Shaders")) ShaderManager::CheckIfShadersHaveChanged();
		}
		ImGui::End();
	}

	void Editor::StatsAndProfiling()
    {
		if (!window_flags[Flag_Profiler]) return;
		if (ImGui::Begin("Profiling", &window_flags[Flag_Profiler]))
        {
            ImGuiIO io = ImGui::GetIO();
            static bool enable_profiling = false;
			ImGui::Checkbox("Enable Profiling", &enable_profiling);
			
			static bool cur_enable_profiling = false;
			if (cur_enable_profiling != enable_profiling)
			{
				cur_enable_profiling = enable_profiling;
				g_GfxProfiler.Initialize(engine->gfx.get());
			}
			
			if (enable_profiling)
			{
				static ProfilerState state;
				static constexpr uint64 NUM_FRAMES = 128;
				static float FRAME_TIME_ARRAY[NUM_FRAMES] = { 0 };
				static float RECENT_HIGHEST_FRAME_TIME = 0.0f;
				static constexpr int32 FRAME_TIME_GRAPH_MAX_FPS[] = { 800, 240, 120, 90, 65, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
				static float FRAME_TIME_GRAPH_MAX_VALUES[ARRAYSIZE(FRAME_TIME_GRAPH_MAX_FPS)] = { 0 };
				for (uint64 i = 0; i < ARRAYSIZE(FRAME_TIME_GRAPH_MAX_FPS); ++i) { FRAME_TIME_GRAPH_MAX_VALUES[i] = 1000.f / FRAME_TIME_GRAPH_MAX_FPS[i]; }

				std::vector<Timestamp> time_stamps = engine->renderer->GetProfilerResults();
				FRAME_TIME_ARRAY[NUM_FRAMES - 1] = 1000.0f / io.Framerate;
				for (uint32 i = 0; i < NUM_FRAMES - 1; i++) FRAME_TIME_ARRAY[i] = FRAME_TIME_ARRAY[i + 1];
				RECENT_HIGHEST_FRAME_TIME = std::max(RECENT_HIGHEST_FRAME_TIME, FRAME_TIME_ARRAY[NUM_FRAMES - 1]);
				float frameTime_ms = FRAME_TIME_ARRAY[NUM_FRAMES - 1];
				const int32 fps = static_cast<int32>(1000.0f / frameTime_ms);

				ImGui::Text("FPS        : %d (%.2f ms)", fps, frameTime_ms);
				if (ImGui::CollapsingHeader("Timings", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Show Avg/Min/Max", &state.show_average);
					ImGui::Spacing();

					uint64 i_max = 0;
					for (uint64 i = 0; i < ARRAYSIZE(FRAME_TIME_GRAPH_MAX_VALUES); ++i)
					{
						if (RECENT_HIGHEST_FRAME_TIME < FRAME_TIME_GRAPH_MAX_VALUES[i]) // FRAME_TIME_GRAPH_MAX_VALUES are in increasing order
						{
							i_max = std::min(ARRAYSIZE(FRAME_TIME_GRAPH_MAX_VALUES) - 1, i + 1);
							break;
						}
					}
					ImGui::PlotLines("",FRAME_TIME_ARRAY, NUM_FRAMES, 0, "GPU frame time (ms)", 0.0f, FRAME_TIME_GRAPH_MAX_VALUES[i_max], ImVec2(0, 80));

					constexpr uint32_t avg_timestamp_update_interval = 1000;
					static auto MillisecondsNow = []()
					{
						static LARGE_INTEGER s_frequency;
						static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
						double milliseconds = 0;
						if (s_use_qpc)
						{
							LARGE_INTEGER now;
							QueryPerformanceCounter(&now);
							milliseconds = double(1000.0 * now.QuadPart) / s_frequency.QuadPart;
						}
						else milliseconds = double(GetTickCount64());
						return milliseconds;
					};
					const double current_time = MillisecondsNow();

					bool reset_accumulating_state = false;
					if ((state.accumulating_frame_count > 1) &&
						((current_time - state.last_reset_time) > avg_timestamp_update_interval))
					{
						std::swap(state.displayed_timestamps, state.accumulating_timestamps);
						for (uint32_t i = 0; i < state.displayed_timestamps.size(); i++)
						{
							state.displayed_timestamps[i].sum /= state.accumulating_frame_count;
						}
						reset_accumulating_state = true;
					}

					reset_accumulating_state |= (state.accumulating_timestamps.size() != time_stamps.size());
					if (reset_accumulating_state)
					{
						state.accumulating_timestamps.resize(0);
						state.accumulating_timestamps.resize(time_stamps.size());
						state.last_reset_time = current_time;
						state.accumulating_frame_count = 0;
					}

					for (uint64 i = 0; i < time_stamps.size(); i++)
					{
						float value = time_stamps[i].time_in_ms;
						char const* pStrUnit = "ms";
						ImGui::Text("%-18s: %7.2f %s", time_stamps[i].name.c_str(), value, pStrUnit);
						if (state.show_average)
						{
							if (state.displayed_timestamps.size() == time_stamps.size())
							{
								ImGui::SameLine();
								ImGui::Text("  avg: %7.2f %s", state.displayed_timestamps[i].sum, pStrUnit);
								ImGui::SameLine();
								ImGui::Text("  min: %7.2f %s", state.displayed_timestamps[i].minimum, pStrUnit);
								ImGui::SameLine();
								ImGui::Text("  max: %7.2f %s", state.displayed_timestamps[i].maximum, pStrUnit);
							}

							ProfilerState::AccumulatedTimeStamp* pAccumulatingTimeStamp = &state.accumulating_timestamps[i];
							pAccumulatingTimeStamp->sum += time_stamps[i].time_in_ms;
							pAccumulatingTimeStamp->minimum = std::min<float>(pAccumulatingTimeStamp->minimum, time_stamps[i].time_in_ms);
							pAccumulatingTimeStamp->maximum = std::max<float>(pAccumulatingTimeStamp->maximum, time_stamps[i].time_in_ms);
						}
					}
					state.accumulating_frame_count++;
				}
			}
			engine->renderer->SetProfiling(enable_profiling);
        }
        ImGui::End();
    }
}
